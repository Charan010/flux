#include "coordinator.h"
#include "chunk.h"

#include <fstream>
#include <vector>


Coordinator::Coordinator(size_t threads, size_t chunk):
    pool(threads), chunk_size(chunk) {}


void Coordinator::compress(const std::string &input_file, const std::string &output_file){

    FrequencyCounter counter(pool);

    std::ifstream in(input_file, std::ios::binary);

    int chunk_id = 0;

    while(true){

        std::vector<uint8_t> data(chunk_size);

        in.read((char*)data.data(), chunk_size);
        size_t read = in.gcount();

        if(read == 0)
            break;

        data.resize(read);

        Chunk chunk(chunk_id++, std::move(data));

        counter.submit_chunk(chunk);
    }

    counter.wait();

    auto freq = counter.get_result();

    Node* root = build_huffman_tree(freq);

    std::array<std::string,256> table;
    build_codes(root, "", table);

    BitWriter bw(output_file);

    write_tree(root ,bw);

    uint32_t total_len = 0;
    for(int i=0;i<256;i++)
        total_len += freq[i];

    write_uint32(bw,total_len);

    ChunkBuffer buffer(bw);

    std::ifstream in2(input_file,std::ios::binary);

    chunk_id = 0;

    while(true)
    {
        std::vector<uint8_t> data(chunk_size);

        in2.read((char*)data.data(),chunk_size);
        size_t read = in2.gcount();

        if(read == 0)
            break;

        data.resize(read);

        int id = chunk_id++;

        pool.submit([data,id,&buffer ,&table]() mutable {

            std::vector<uint8_t> encoded;

            encoded.reserve(data.size() * 4);

            for(uint8_t c : data){
                const std::string& code = table[c];

                for(char b : code)
                    encoded.push_back(b == '1');
            }

            Chunk chunk(id, std::move(encoded));

            buffer.submit_chunk(std::move(chunk));
        });
    }

    pool.shutdown();

    buffer.flush_ready_chunks();

    bw.flush();

    free_tree(root);
}

void Coordinator::decompress(const std::string &input_file, const std::string &dump_file){
    
    BitReader br(input_file);

    Node* root = read_tree(br);
    uint32_t text_len = read_uint32(br);


    std::ofstream out(dump_file, std::ios::binary);
    Node* curr = root;

    uint32_t produced = 0;

    while(produced < text_len)
    {
        int bit = br.read_bit();

        if(bit == -1)
            throw std::runtime_error("corrupt compressed file");

        if(bit == 0)
            curr = curr->left;
        else
            curr = curr->right;

        if(!curr->left && !curr->right)
        {
            out.put(static_cast<char>(curr->ch));
            curr = root;
            produced++;
        }
    }

    out.close();

    free_tree(root);
}



