#include "codec.h"

#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

#include <zstd.h>

namespace {

constexpr int kZstdLevel = 3;   
constexpr size_t kMinSavingsDivisor = 20;
   

struct CCtxDeleter{
	 void operator()(ZSTD_CCtx *p)const{
		 ZSTD_freeCCtx(p); 
	  } 	

};

struct DCtxDeleter{
	 void operator()(ZSTD_DCtx *p)const{
		 ZSTD_freeDCtx(p); 
	 } 
};

ZSTD_CCtx *compress_ctx() {
	
    static thread_local std::unique_ptr<ZSTD_CCtx, CCtxDeleter> ctx(ZSTD_createCCtx());
    if (!ctx)
        throw std::runtime_error("zstd: failed to create compression context");
    return ctx.get();
}

ZSTD_DCtx *decompress_ctx(){

    static thread_local std::unique_ptr<ZSTD_DCtx, DCtxDeleter> ctx(ZSTD_createDCtx());

    if (!ctx)
        throw std::runtime_error("zstd: failed to create decompression context");
    return ctx.get();

}

} 

const char *codec_name(CodecId codec) {
    switch (codec) {
    case CodecId::Raw:
		return "raw";

    case CodecId::Zstd:
		return "zstd";

    }
	
    return "unknown";
}

CodecId codec_compress(const uint8_t *input, size_t input_size,
                       std::vector<uint8_t> &output) {

    if (input_size == 0) {
        output.clear();
        return CodecId::Raw;
    }

    output.resize(ZSTD_compressBound(input_size));

    const size_t written = ZSTD_compressCCtx(compress_ctx(),output.data(), output.size(), input, input_size, kZstdLevel);
    if (ZSTD_isError(written))
        throw std::runtime_error(std::string("zstd compress: ") + ZSTD_getErrorName(written));

    if (written + input_size / kMinSavingsDivisor >= input_size) {
        output.assign(input, input + input_size);
        return CodecId::Raw;
    }

    output.resize(written);
    return CodecId::Zstd;
}

void codec_decompress(CodecId codec, const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size) {
    
	switch (codec) {
    	case CodecId::Raw:
        	if (input_size != output_size)
            	throw std::runtime_error("raw codec: stored size does not match original size");
        		std::memcpy(output, input, input_size);
        		return;

    	case CodecId::Zstd: {
        	const size_t decoded = ZSTD_decompressDCtx(decompress_ctx(), output, output_size, input, input_size);
        	if (ZSTD_isError(decoded))
            	throw std::runtime_error(std::string("zstd decompress: ") + ZSTD_getErrorName(decoded));

        	if (decoded != output_size)
	            throw std::runtime_error("zstd decompress: size mismatch");
        	return;

    	}

    }

    throw std::runtime_error("unknown codec id");
}