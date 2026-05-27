package main

import (
	"bufio"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"
)

const (
	daemonSocket = "/tmp/flux.sock"
	tempDir      = "/tmp/flux_uploads"
	listenAddr   = ":8080"
)

type daemonRequest struct {
	Action string `json:"action"`
	Codec  string `json:"codec,omitempty"`
	Input  string `json:"input"`
	Output string `json:"output"`
}

type daemonResponse struct {
	Status         string  `json:"status"`
	Output         string  `json:"output"`
	InputSize      uint64  `json:"input_size"`
	CompressedSize uint64  `json:"compressed_size"`
	OutputSize     uint64  `json:"output_size"`
	Ratio          float64 `json:"ratio"`
	Message        string  `json:"message"`
}

func sendToDaemon(req daemonRequest) (*daemonResponse, error) {
	conn, err := net.Dial("unix", daemonSocket)
	if err != nil {
		return nil, fmt.Errorf("cannot connect to flux daemon: %w", err)
	}
	defer conn.Close()

	payload, err := json.Marshal(req)
	if err != nil {
		return nil, err
	}
	payload = append(payload, '\n')

	if _, err := conn.Write(payload); err != nil {
		return nil, fmt.Errorf("write to daemon failed: %w", err)
	}

	// Block until daemon writes the response line (fires after flush())
	scanner := bufio.NewScanner(conn)
	if !scanner.Scan() {
		if err := scanner.Err(); err != nil {
			return nil, fmt.Errorf("reading daemon response: %w", err)
		}
		return nil, fmt.Errorf("daemon closed connection without response")
	}

	var res daemonResponse
	if err := json.Unmarshal(scanner.Bytes(), &res); err != nil {
		return nil, fmt.Errorf("invalid daemon response: %w", err)
	}

	return &res, nil
}

func tempFilePath(ext string) string {
	return filepath.Join(tempDir, fmt.Sprintf("flux_%d%s", time.Now().UnixNano(), ext))
}

func cleanup(paths ...string) {
	for _, p := range paths {
		os.Remove(p)
	}
}

func handleCompress(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	codec := r.URL.Query().Get("codec")
	if codec == "" {
		codec = "lz4"
	}
	if codec != "lz4" && codec != "huffman" {
		http.Error(w, "unknown codec — use lz4 or huffman", http.StatusBadRequest)
		return
	}

	if err := r.ParseMultipartForm(10 << 30); err != nil {
		http.Error(w, "failed to parse multipart form", http.StatusBadRequest)
		return
	}

	file, header, err := r.FormFile("file")
	if err != nil {
		http.Error(w, "missing 'file' field in form", http.StatusBadRequest)
		return
	}
	defer file.Close()

	ext := ".lz4"
	if codec == "huffman" {
		ext = ".huff"
	}

	inputPath := tempFilePath(filepath.Ext(header.Filename))
	outputPath := tempFilePath(ext)
	defer cleanup(inputPath, outputPath)

	dst, err := os.Create(inputPath)
	if err != nil {
		http.Error(w, "failed to create temp file", http.StatusInternalServerError)
		return
	}

	if _, err := io.Copy(dst, file); err != nil {
		dst.Close()
		http.Error(w, "failed to save upload", http.StatusInternalServerError)
		return
	}
	dst.Close()

	// Send job to C++ daemon — blocks until file is fully compressed and on disk
	res, err := sendToDaemon(daemonRequest{
		Action: "compress",
		Codec:  codec,
		Input:  inputPath,
		Output: outputPath,
	})
	if err != nil {
		log.Printf("daemon error: %v", err)
		http.Error(w, "compression failed", http.StatusInternalServerError)
		return
	}
	if res.Status != "ok" {
		http.Error(w, "compression failed: "+res.Message, http.StatusInternalServerError)
		return
	}

	// Stream compressed file back to client
	outFile, err := os.Open(outputPath)
	if err != nil {
		http.Error(w, "output file missing", http.StatusInternalServerError)
		return
	}
	defer outFile.Close()

	originalName := strings.TrimSuffix(header.Filename, filepath.Ext(header.Filename))
	downloadName := originalName + ext

	w.Header().Set("Content-Disposition", fmt.Sprintf(`attachment; filename="%s"`, downloadName))
	w.Header().Set("Content-Type", "application/octet-stream")
	w.Header().Set("X-Original-Size", fmt.Sprintf("%d", res.InputSize))
	w.Header().Set("X-Compressed-Size", fmt.Sprintf("%d", res.CompressedSize))
	w.Header().Set("X-Ratio", fmt.Sprintf("%.2f", res.Ratio))

	if stat, err := outFile.Stat(); err == nil {
		w.Header().Set("Content-Length", fmt.Sprintf("%d", stat.Size()))
	}

	io.Copy(w, outFile)

	log.Printf("compress [%s] %s → %s  %.2fx  in=%.1fMB out=%.1fMB",
		codec, header.Filename, downloadName,
		res.Ratio,
		float64(res.InputSize)/(1024*1024),
		float64(res.CompressedSize)/(1024*1024),
	)
}

// handleDecompress handles POST /decompress
// Expects multipart form with a "file" field (.lz4 or .huff).
// Streams the decompressed file back to the client.
func handleDecompress(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	if err := r.ParseMultipartForm(10 << 30); err != nil {
		http.Error(w, "failed to parse multipart form", http.StatusBadRequest)
		return
	}

	file, header, err := r.FormFile("file")
	if err != nil {
		http.Error(w, "missing 'file' field in form", http.StatusBadRequest)
		return
	}
	defer file.Close()

	inputPath := tempFilePath(filepath.Ext(header.Filename))
	outputPath := tempFilePath(".out")
	defer cleanup(inputPath, outputPath)

	dst, err := os.Create(inputPath)
	if err != nil {
		http.Error(w, "failed to create temp file", http.StatusInternalServerError)
		return
	}

	if _, err := io.Copy(dst, file); err != nil {
		dst.Close()
		http.Error(w, "failed to save upload", http.StatusInternalServerError)
		return
	}
	dst.Close()

	res, err := sendToDaemon(daemonRequest{
		Action: "decompress",
		Input:  inputPath,
		Output: outputPath,
	})
	if err != nil {
		log.Printf("daemon error: %v", err)
		http.Error(w, "decompression failed", http.StatusInternalServerError)
		return
	}
	if res.Status != "ok" {
		http.Error(w, "decompression failed: "+res.Message, http.StatusInternalServerError)
		return
	}

	outFile, err := os.Open(outputPath)
	if err != nil {
		http.Error(w, "output file missing", http.StatusInternalServerError)
		return
	}
	defer outFile.Close()

	// Strip the .lz4 / .huff extension for the download name
	originalExt := filepath.Ext(header.Filename)
	downloadName := strings.TrimSuffix(header.Filename, originalExt)
	if downloadName == "" {
		downloadName = "decompressed"
	}

	w.Header().Set("Content-Disposition", fmt.Sprintf(`attachment; filename="%s"`, downloadName))
	w.Header().Set("Content-Type", "application/octet-stream")
	w.Header().Set("X-Output-Size", fmt.Sprintf("%d", res.OutputSize))

	if stat, err := outFile.Stat(); err == nil {
		w.Header().Set("Content-Length", fmt.Sprintf("%d", stat.Size()))
	}

	io.Copy(w, outFile)

	log.Printf("decompress %s → %s  out=%.1fMB",
		header.Filename, downloadName,
		float64(res.OutputSize)/(1024*1024),
	)
}

func handlePing(w http.ResponseWriter, r *http.Request) {

	conn, err := net.DialTimeout("unix", daemonSocket, time.Second)
	if err != nil {
		w.WriteHeader(http.StatusServiceUnavailable)
		fmt.Fprintf(w, `{"status":"daemon unreachable"}`)
		return
	}
	conn.Close()
	w.Header().Set("Content-Type", "application/json")
	fmt.Fprintf(w, `{"status":"ok"}`)
}

func sha256File(path string) (string, error) {

	f, err := os.Open(path)
	if err != nil {
		return "", err
	}
	defer f.Close()

	h := sha256.New()

	if _, err := io.Copy(h, f); err != nil {
		return "", err
	}

	return hex.EncodeToString(h.Sum(nil)), nil
}
func handleVerify(w http.ResponseWriter, r *http.Request) {

	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	if err := r.ParseMultipartForm(20 << 30); err != nil {
		http.Error(w, "failed to parse multipart form", http.StatusBadRequest)
		return
	}

	file1, header1, err := r.FormFile("file1")
	if err != nil {
		http.Error(w, "missing file1", http.StatusBadRequest)
		return
	}
	defer file1.Close()

	file2, header2, err := r.FormFile("file2")
	if err != nil {
		http.Error(w, "missing file2", http.StatusBadRequest)
		return
	}
	defer file2.Close()

	path1 := tempFilePath(filepath.Ext(header1.Filename))
	path2 := tempFilePath(filepath.Ext(header2.Filename))

	defer cleanup(path1, path2)

	dst1, err := os.Create(path1)
	if err != nil {
		http.Error(w, "failed to create temp file", http.StatusInternalServerError)
		return
	}

	dst2, err := os.Create(path2)
	if err != nil {
		http.Error(w, "failed to create temp file", http.StatusInternalServerError)
		return
	}

	io.Copy(dst1, file1)
	io.Copy(dst2, file2)

	dst1.Close()
	dst2.Close()

	hash1, err := sha256File(path1)
	if err != nil {
		http.Error(w, "hash failed", http.StatusInternalServerError)
		return
	}

	hash2, err := sha256File(path2)
	if err != nil {
		http.Error(w, "hash failed", http.StatusInternalServerError)
		return
	}

	type verifyResponse struct {
		Match bool   `json:"match"`
		Hash1 string `json:"hash1"`
		Hash2 string `json:"hash2"`
	}

	res := verifyResponse{
		Match: hash1 == hash2,
		Hash1: hash1,
		Hash2: hash2,
	}

	w.Header().Set("Content-Type", "application/json")

	json.NewEncoder(w).Encode(res)
}

func handleIndex(w http.ResponseWriter, r *http.Request) {

	if r.URL.Path != "/" {
		http.NotFound(w, r)
		return
	}

	http.ServeFile(w, r, "home.html")
}

func main() {

	if err := os.MkdirAll(tempDir, 0755); err != nil {
		log.Fatalf("cannot create temp dir %s: %v", tempDir, err)
	}

	if _, err := os.Stat(daemonSocket); err != nil {
		log.Fatalf("flux daemon socket not found at %s — run 'flux --daemon' first", daemonSocket)
	}

	mux := http.NewServeMux()
	mux.HandleFunc("/compress", handleCompress)
	mux.HandleFunc("/decompress", handleDecompress)
	mux.HandleFunc("/ping", handlePing)
	mux.HandleFunc("/", handleIndex)
	mux.HandleFunc("/verify", handleVerify)

	log.Printf("flux HTTP server listening on %s", listenAddr)
	log.Printf("  POST /compress?codec=lz4|huffman  (multipart file)")
	log.Printf("  POST /decompress                  (multipart file)")
	log.Printf("  GET  /ping")

	log.Fatal(http.ListenAndServe(listenAddr, mux))
}
