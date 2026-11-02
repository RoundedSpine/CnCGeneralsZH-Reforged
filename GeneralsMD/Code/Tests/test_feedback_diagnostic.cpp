// Actual diagnostic writer and one-shot gate, independent of a GPU/device SDK.
#include "FeedbackDiagnostic.h"
#include <cassert>
#include <string>
int main() {
    FeedbackDiagnostic::Record r;
    r.presents=37;r.draws=1234;r.slot=3;r.textureStage=6;r.samplerStage=2;
    r.width=256;r.height=256;r.targetFormat=21;r.sampleWidth=256;r.sampleHeight=256;
    r.sameLogicalTexture=true;r.depth=false;r.vertexName="fixed-function";r.pixelName="water-test";
    const std::string longKey(10000,'x');r.pixelKey=longKey.c_str();
    FILE *log=tmpfile();assert(log);
    FeedbackDiagnostic::Once once;
    int reports=0;
    for(int i=0;i<10000;++i) if(once.claim()) { FeedbackDiagnostic::write(log,r);++reports; }
    assert(reports==1);
    fflush(log);assert(ftell(log)<4096);rewind(log);
    std::string text;char bytes[1024];size_t count;
    while((count=fread(bytes,1,sizeof(bytes),log))) text.append(bytes,count);
    fclose(log);
    assert(text.find("present=37 recorded_draws=1234")!=std::string::npos);
    assert(text.find("slot=3 texture_stage=6 sampler_stage=2")!=std::string::npos);
    assert(text.find("target=256x256 format=21")!=std::string::npos);
    assert(text.find("same_logical_texture=1")!=std::string::npos);
    assert(text.find("pixel=water-test")!=std::string::npos);
    assert(r.presents==37 && r.textureStage==6 && r.sameLogicalTexture);
    FeedbackDiagnostic::Once newDevice;assert(newDevice.claim());
}
