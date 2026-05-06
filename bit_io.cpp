        #include "bit_io.h"
        #include <cassert>
        #include <cstring>

        using namespace std;

        BitWriter::BitWriter(const std::string& file):
         io_buf(std::make_unique<char[]>(BUF_SIZE)), out(file, std::ios::binary) {
        
            out.rdbuf()->pubsetbuf(io_buf.get(), BUF_SIZE);
        }


        void BitWriter::write_bits(uint64_t bits, int count){

            if(count == 0)
                return;

            assert(count > 0 && count <= 64);
            assert(bits_in_acc + count <= 64);

            if(count < 64)
                bits &= (1ULL << count)-1;

            acc = (acc << count) | bits;
            bits_in_acc += count;

             uint8_t tmp[8];
             int t = 0;

            while (bits_in_acc >= 8) {
                bits_in_acc -= 8;
                tmp[t++] = (acc >> bits_in_acc) & 0xFF;
            }

            if (t > 0)
                out.write(reinterpret_cast<char*>(tmp), t);

            acc &= (bits_in_acc > 0) ? (1ULL << bits_in_acc) - 1 : 0;

        }
    
        void BitWriter::write_bit(int b){
            assert(b == 0 || b == 1);
            write_bits(b, 1);

        }
        
        void BitWriter::write_byte(uint8_t b) {
            write_bits(b, 8);
        }

        void BitWriter::flush(){
            if(bits_in_acc > 0){

                assert(bits_in_acc < 8);

                uint8_t byte = (acc << (8 - bits_in_acc)) & 0xFF;
                out.put(static_cast<char>(byte));

            }
            acc = 0;
            bits_in_acc = 0;

        }
        


        void BitWriter::write_bytes(const std::vector<uint8_t>& data) {

            assert(bits_in_acc == 0 && "write_bytes called with unflused bits");
            out.write(reinterpret_cast<const char*>(data.data()), data.size());
        }


        BitReader::BitReader(const std::string& file)
            : in(file, std::ios::binary) {
            in.rdbuf()->pubsetbuf(io_buf.get(), sizeof(io_buf));  
        }

        void BitReader::refill(int needed){
            assert(needed >= 0 && needed <= 64); 

            while(bits_in_buf < needed && bits_in_buf <= 56){
                char c;
                if (!in.get(c)) 
                    break;
                
                bitbuf = (bitbuf << 8) | static_cast<uint8_t>(c);
                bits_in_buf += 8;
            }
        }


        uint32_t BitReader::peek_bit(int n){

            assert(n > 0 && n <= 32);
            refill(n);

            if(bits_in_buf < n)
                throw std::runtime_error("unexpected EOF");

            return (bitbuf >> (bits_in_buf - n)) & ((1u << n) - 1);

        }

        void BitReader::consume_bits(int n){
            assert(n >= 0 && n <= bits_in_buf);
            bits_in_buf -= n;
        }

        int BitReader::read_bit(){

            refill(1);

            if(bits_in_buf == 0)
                return -1;

            int bit = (bitbuf >> (bits_in_buf - 1)) & 1;
            bits_in_buf--;

            return bit;
        }


        uint8_t BitReader::read_byte(){
            refill(8);

            if(bits_in_buf < 8)
                throw std::runtime_error("unexpected EOF");

            uint8_t val = (bitbuf >> (bits_in_buf - 8)) & 0xFF;
            bits_in_buf -= 8;

            return val;
        }


        void BitReader::align_to_byte(){
            bits_in_buf -= bits_in_buf % 8;
        }

        void BitReader::read_bytes(uint8_t* dst, size_t n){

            align_to_byte();
            in.read(reinterpret_cast<char*>(dst), n);

            if (in.gcount() != static_cast<std::streamsize>(n))
                throw std::runtime_error("unexpected EOF in read_bytes");
        
        }



