    #include "bit_io.h"


    BitWriter::BitWriter(const std::string& file)
        : out(file, std::ios::binary) {
        out.rdbuf()->pubsetbuf(io_buf, sizeof(io_buf));  
    }

    void BitWriter::write_bits(uint64_t bits, int count) {
        acc = (acc << count) | (bits & ((1ULL << count) - 1));
        bits_in_acc += count;

        int bytes_ready = bits_in_acc / 8;
        if (bytes_ready > 0) {
            int shift = bits_in_acc - (bytes_ready * 8);
            uint64_t to_write = acc >> shift;


            uint8_t buf[8];
            for (int i = bytes_ready - 1; i >= 0; i--) {
                buf[i] = to_write & 0xFF;
                to_write >>= 8;
            }
            out.write(reinterpret_cast<const char*>(buf), bytes_ready);

            bits_in_acc = shift;
            if (shift < 64)
                acc &= (1ULL << shift) - 1;
            else
            acc &= (1ULL << shift) - 1;
        }
    }

    void BitWriter::write_bit(int b) {
        write_bits(b, 1);
    }

    void BitWriter::write_byte(uint8_t b) {
        write_bits(b, 8);
    }

    void BitWriter::flush() {
        if (bits_in_acc > 0) {
            out.put(static_cast<uint8_t>((acc << (8 - bits_in_acc)) & 0xFF));
            acc = 0;
            bits_in_acc = 0;
        }
        out.flush();  
    }

    void BitWriter::write_bytes(const std::vector<uint8_t>& data) {
        flush(); 
        out.write(reinterpret_cast<const char*>(data.data()), data.size());
    }


    BitReader::BitReader(const std::string& file)
        : in(file, std::ios::binary) {
        in.rdbuf()->pubsetbuf(io_buf, sizeof(io_buf));  
    }

    void BitReader::refill(int needed){

        while(bits_in_buf < needed){

            if(bits_in_buf + 8 > 64)
                break;

            int c = in.get();
            if(c == EOF)
                break;

            bitbuf = (bitbuf << 8) |(uint8_t)c;
            bits_in_buf += 8;
        }

    }

    uint32_t BitReader::peek_bit(int n){

        refill(n);

        if(bits_in_buf < n)
            throw std::runtime_error("unexpected EOF");

        return (bitbuf >> (bits_in_buf - n)) & ((1u << n) - 1);

    }

    void BitReader::consume_bits(int n){
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



