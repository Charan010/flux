#include "huffman.h"
#include "bit_io.h"
#include <array>


Node::Node(uint8_t c, uint64_t f) {
    ch = c;
    freq = f;
    left = nullptr;
    right = nullptr;
}

Node::Node(Node* l, Node* r) {
    ch = '\0';
    freq = l->freq + r->freq;
    left = l;
    right = r;
}


bool Compare::operator()(Node* a, Node* b) {
    return a->freq > b->freq;
}


/* using a priority queue to take minimum frequent character first and build tree. So, that characters
   with more frequency get shorter code instead of fixed code for all characters.

    For example:
        If my entire text/string uses only A,B,C,D,E
        then i need atleast 3 bits to encode characters.

        so total bits sent = 3 * length of the text/string.

    But according to information theory, I(x) is inversely proportional to P(x)
    So, more likely some event is to occur then less information we will gain.
   
    
*/

Node *build_huffman_tree(const FrequencyTable &freq){


    std::priority_queue<Node*, std::vector<Node*>, Compare> pq;

    for(int i = 0; i < 256; i++) {
        if(freq[i] > 0)
        pq.push(new Node((char)i, freq[i]));
}

    while(pq.size() > 1){
        Node *a = pq.top();
        pq.pop();

        Node *b = pq.top();
        pq.pop();

        pq.push(new Node(a, b));
    }

    // return root tree after building 
    return pq.top();
}


void build_codes(Node* root_node, std::string code, std::array<std::string,256>& table){
    if(!root_node)
        return;

    if(!root_node->left && !root_node->right){
        table[(uint8_t)root_node->ch] = code.empty() ? "0" : code;
        return;
    }

    build_codes(root_node->left,  code + "0", table);
    build_codes(root_node->right, code + "1", table);
}


void write_tree(Node *node, BitWriter &bw){
    if(!node)
        return;

    if(!node -> left && !node -> right){
        bw.write_bit(1);
        bw.write_byte(node -> ch);
        return;
    }
    
    bw.write_bit(0);
    write_tree(node -> left, bw);
    write_tree(node -> right, bw);
}

Node* read_tree(BitReader &br){
    int bit = br.read_bit();

    if(bit == 1){

        uint8_t ch = br.read_byte();
        return new Node(ch, 0);
        
    }

    Node* left = read_tree(br);
    Node* right = read_tree(br);

    return new Node(left, right);
}


void free_tree(Node* node) {
    if (!node) return;
    free_tree(node->left);
    free_tree(node->right);
    delete node;
}

uint32_t read_uint32(BitReader& br) {
    uint32_t x = 0;
    for (int i = 0; i < 4; i++)
        x = (x << 8) | br.read_byte();
    return x;
}


void write_uint32(BitWriter& bw, uint32_t x) {
    bw.write_byte((x >> 24) & 0xFF);
    bw.write_byte((x >> 16) & 0xFF);
    bw.write_byte((x >> 8) & 0xFF);
    bw.write_byte(x & 0xFF);
}

