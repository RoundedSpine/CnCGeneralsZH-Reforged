// Bounded diagnostic data for an attachment/sampler conflict. No renderer mutation.
#pragma once
#include <cstdio>
namespace FeedbackDiagnostic {
struct Record {
    unsigned presents=0, draws=0, slot=0, textureStage=0, samplerStage=0;
    unsigned width=0, height=0, targetFormat=0, sampleWidth=0, sampleHeight=0, sampleFormat=0;
    unsigned primitiveType=0, primitives=0, fvf=0, colourWrite=0, zEnable=0, zWrite=0, blend=0;
    unsigned colourOp=0, colourArg1=0, colourArg2=0, alphaOp=0, alphaArg1=0, alphaArg2=0;
    bool depth=false, backBuffer=false, sameLogicalTexture=false, sun=false;
    double elapsedMs=0;
    const char *vertexName="", *pixelName="", *vertexKey="", *pixelKey="";
    const void *texture=nullptr, *surface=nullptr, *container=nullptr;
    const void *sampleGPU=nullptr, *colourGPU=nullptr, *depthGPU=nullptr;
};
class Once {
    bool reported=false;
public:
    bool claim() { if (reported) return false; reported=true; return true; }
};
inline void write(FILE *out,const Record &r) {
    fprintf(out,"[GPU_FEEDBACK] v=1 elapsed_ms=%.1f present=%u recorded_draws=%u attachment=%s back_buffer=%d sun=%d slot=%u texture_stage=%u sampler_stage=%u\n",
        r.elapsedMs,r.presents,r.draws,r.depth?"depth":"colour",r.backBuffer,r.sun,r.slot,r.textureStage,r.samplerStage);
    fprintf(out,"[GPU_FEEDBACK_RESOURCE] target=%ux%u format=%u sampled=%ux%u format=%u same_logical_texture=%d cpu_texture=%p cpu_surface=%p cpu_container=%p gpu_sample=%p gpu_colour=%p gpu_depth=%p\n",
        r.width,r.height,r.targetFormat,r.sampleWidth,r.sampleHeight,r.sampleFormat,r.sameLogicalTexture,
        r.texture,r.surface,r.container,r.sampleGPU,r.colourGPU,r.depthGPU);
    fprintf(out,"[GPU_FEEDBACK_DRAW] primitive_type=%u primitives=%u fvf=0x%x colour_write=0x%x z=%u/%u blend=%u colour_op=%u args=0x%x/0x%x alpha_op=%u args=0x%x/0x%x\n",
        r.primitiveType,r.primitives,r.fvf,r.colourWrite,r.zEnable,r.zWrite,r.blend,
        r.colourOp,r.colourArg1,r.colourArg2,r.alphaOp,r.alphaArg1,r.alphaArg2);
    fprintf(out,"[GPU_FEEDBACK_SHADER] vertex=%.256s pixel=%.256s\n",r.vertexName,r.pixelName);
    fprintf(out,"[GPU_FEEDBACK_PROGRAM] vertex=%.512s\n[GPU_FEEDBACK_PROGRAM] pixel=%.512s\n",r.vertexKey,r.pixelKey);
}
}
