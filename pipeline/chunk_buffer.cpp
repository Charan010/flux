#include "huffman.h"
#include "chunk.h"
#include <fstream>
#include "bit_io.h"
#include <queue>


void write_chunk_to_file(const std::vector<uint8_t> &data, std::ofstream &output_file){

    for(char c: data){
        BitWriter::write_bit(c == 1);
    }




}



void chunk_buffer(std::ofstream &output_file, int total_chunks){

    std::priority_queue<Chunk*> buffer;

    int expected_chunk_id = 0;

    while(!buffer.empty()){

        Chunk* chunk = buffer.top();

        if(chunk -> id == expected_chunk_id){

        }





    }








}


