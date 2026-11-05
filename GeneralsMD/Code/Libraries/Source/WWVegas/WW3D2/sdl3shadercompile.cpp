/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "sdl3shadercompile.h"

#include <SDL3/SDL.h>
#include <SDL3_shadercross/SDL_shadercross.h>
#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>

#include <string.h>

#include <mutex>

namespace {

// Both libraries want one process-wide initialisation and neither says what happens on a second.
std::once_flag glslang_ready;
std::once_flag shadercross_ready;
bool shadercross_ok = false;

void start_shadercross()
{
	std::call_once(shadercross_ready, []() { shadercross_ok = SDL_ShaderCross_Init(); });
}

SDL_ShaderCross_SPIRV_Info spirv_info(const std::vector<unsigned char> & spirv, SdlShaderStage stage)
{
	SDL_ShaderCross_SPIRV_Info info;
	SDL_zero(info);
	info.bytecode = spirv.empty() ? NULL : &spirv[0];
	info.bytecode_size = spirv.size();
	info.entrypoint = "main";
	info.shader_stage = stage==SdlShaderStage::Vertex ? SDL_SHADERCROSS_SHADERSTAGE_VERTEX
        : stage==SdlShaderStage::Compute ? SDL_SHADERCROSS_SHADERSTAGE_COMPUTE : SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT;
	return info;
}

} // namespace

namespace {

// Counts the resources a program's SPIR-V binds, the way SDL numbers them, straight from the
// instructions.  No reflection library: SDL_shadercross counts what a program uses, SDL needs
// the highest slot + 1, and shadercross's reflection sorts a texel-fetch-only texture (.Load,
// no sampler) with the storage textures.  Types are followed, so a texture is told from a
// storage texture by its OpTypeImage "Sampled" operand and a storage buffer from a constant
// buffer by BufferBlock / the StorageBuffer storage class.
//
// SDL's sets: a vertex program's textures, storage textures and storage buffers in set 0 and
// constant buffers in set 1; a pixel program's in 2 and 3.  Storage resources follow the sampled
// textures in set 0 / 2, so samplers here is the highest sampled-texture slot + 1 and does not
// include them.
struct SdlShaderResources
{
	unsigned samplers = 0;
	unsigned storage_textures = 0;
	unsigned storage_buffers = 0;
	unsigned uniform_buffers = 0;
};

namespace sdl3_shader_resources_detail {

inline unsigned word_at(const unsigned char * p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24);
}

enum Kind : unsigned char { KindNone, KindImage, KindStorageImage, KindSampler, KindStruct, KindArray, KindPointer, KindVariable };

struct Info
{
	unsigned set = ~0u, binding = ~0u;
	unsigned storage_class = ~0u;
	unsigned link = 0;		// pointer: pointee type; array: element type; variable: its pointer type
	Kind kind = KindNone;
	bool buffer_block = false;
};

} // namespace sdl3_shader_resources_detail

inline void SDL3_Shader_Resources(const std::vector<unsigned char> & spirv, bool vertex_stage, SdlShaderResources & out)
{
	using namespace sdl3_shader_resources_detail;
	out = SdlShaderResources();
	const size_t words = spirv.size() / 4;
	if (words < 5) return;
	const unsigned char * bytes = &spirv[0];
	const unsigned id_bound = word_at(bytes + 12);
	if (id_bound == 0 || id_bound > (1u << 22)) return;
	const unsigned texture_set = vertex_stage ? 0 : 2;
	const unsigned uniform_set = vertex_stage ? 1 : 3;

	std::vector<Info> info(id_bound);
	for (size_t at = 5; at < words;) {
		const unsigned head = word_at(bytes + at * 4);
		const unsigned opcode = head & 0xffff, length = head >> 16;
		if (length == 0 || at + length > words) break;
		const unsigned char * w = bytes + at * 4;
#define SDL3_OPERAND(n) word_at(w + (n) * 4)
		switch (opcode) {
		case 71:	// OpDecorate target decoration [value]
			if (length >= 3) {
				const unsigned target = SDL3_OPERAND(1), decoration = SDL3_OPERAND(2);
				if (target < id_bound) {
					if (decoration == 3) info[target].buffer_block = true;							// BufferBlock
					if (decoration == 34 && length >= 4) info[target].set = SDL3_OPERAND(3);		// DescriptorSet
					if (decoration == 33 && length >= 4) info[target].binding = SDL3_OPERAND(3);	// Binding
				}
			}
			break;
		case 25:	// OpTypeImage result sampledType dim depth arrayed ms sampled format
			if (length >= 8 && SDL3_OPERAND(1) < id_bound)
				info[SDL3_OPERAND(1)].kind = SDL3_OPERAND(7) == 2 ? KindStorageImage : KindImage;
			break;
		case 26:	// OpTypeSampler
		case 27:	// OpTypeSampledImage
			if (length >= 2 && SDL3_OPERAND(1) < id_bound)
				info[SDL3_OPERAND(1)].kind = opcode == 26 ? KindSampler : KindImage;
			break;
		case 28:	// OpTypeArray result element length
		case 29:	// OpTypeRuntimeArray result element
			if (length >= 3 && SDL3_OPERAND(1) < id_bound) {
				info[SDL3_OPERAND(1)].kind = KindArray;
				info[SDL3_OPERAND(1)].link = SDL3_OPERAND(2);
			}
			break;
		case 30:	// OpTypeStruct
			if (length >= 2 && SDL3_OPERAND(1) < id_bound) info[SDL3_OPERAND(1)].kind = KindStruct;
			break;
		case 32:	// OpTypePointer result storageClass type
			if (length >= 4 && SDL3_OPERAND(1) < id_bound) {
				info[SDL3_OPERAND(1)].kind = KindPointer;
				info[SDL3_OPERAND(1)].storage_class = SDL3_OPERAND(2);
				info[SDL3_OPERAND(1)].link = SDL3_OPERAND(3);
			}
			break;
		case 59:	// OpVariable resultType result storageClass
			if (length >= 4 && SDL3_OPERAND(2) < id_bound) {
				info[SDL3_OPERAND(2)].kind = KindVariable;
				info[SDL3_OPERAND(2)].link = SDL3_OPERAND(1);
				info[SDL3_OPERAND(2)].storage_class = SDL3_OPERAND(3);
			}
			break;
		}
#undef SDL3_OPERAND
		at += length;
	}

	for (unsigned id = 0; id < id_bound; ++id) {
		const Info & var = info[id];
		if (var.kind != KindVariable || var.binding == ~0u || var.set == ~0u) continue;
		if (var.link >= id_bound) continue;
		unsigned type = info[var.link].kind == KindPointer ? info[var.link].link : 0;
		for (int depth = 0; type < id_bound && info[type].kind == KindArray && depth < 8; ++depth) type = info[type].link;
		if (type >= id_bound) continue;
		const Info & t = info[type];
		if (var.set == texture_set) {
			if (t.kind == KindStorageImage) ++out.storage_textures;
			else if (t.kind == KindStruct && (var.storage_class == 12 || (var.storage_class == 2 && t.buffer_block))) ++out.storage_buffers;
			else if (t.kind == KindImage || t.kind == KindSampler) {
				if (var.binding + 1 > out.samplers) out.samplers = var.binding + 1;
			}
		}
		else if (var.set == uniform_set) {
			if (var.binding + 1 > out.uniform_buffers) out.uniform_buffers = var.binding + 1;
		}
	}
}

} // namespace

// The one place the HLSL front end is named (see the header).  glslang's defaults are what the
// D-spike measured all 49 of the game's programs through: Vulkan 1.0, SPIR-V 1.0, default limits.
bool SDL3_Compile_HLSL_To_SPIRV(const std::string & hlsl, bool vertex_stage, std::vector<unsigned char> & spirv,
	std::string & log)
{return SDL3_Compile_HLSL(hlsl,vertex_stage?SdlShaderStage::Vertex:SdlShaderStage::Fragment,spirv,log);}

bool SDL3_Compile_HLSL(const std::string &hlsl,SdlShaderStage shaderStage,std::vector<unsigned char> &spirv,std::string &log)
{
	std::call_once(glslang_ready, []() { glslang_initialize_process(); });
	spirv.clear();
	log.clear();

	const glslang_stage_t stage = shaderStage==SdlShaderStage::Vertex ? GLSLANG_STAGE_VERTEX
        : shaderStage==SdlShaderStage::Compute ? GLSLANG_STAGE_COMPUTE : GLSLANG_STAGE_FRAGMENT;
	glslang_input_t input;
	memset(&input, 0, sizeof(input));
	input.language = GLSLANG_SOURCE_HLSL;
	input.stage = stage;
	input.client = GLSLANG_CLIENT_VULKAN;
	input.client_version = GLSLANG_TARGET_VULKAN_1_0;
	input.target_language = GLSLANG_TARGET_SPV;
	input.target_language_version = GLSLANG_TARGET_SPV_1_0;
	input.code = hlsl.c_str();
	input.default_version = 100;
	input.default_profile = GLSLANG_NO_PROFILE;
	input.messages = GLSLANG_MSG_READ_HLSL_BIT;
	input.resource = glslang_default_resource();

	glslang_shader_t * shader = glslang_shader_create(&input);
	glslang_shader_set_entry_point(shader, "main");
	bool ok = glslang_shader_preprocess(shader, &input) && glslang_shader_parse(shader, &input);
	if (!ok) {
		log = glslang_shader_get_info_log(shader);
		glslang_shader_delete(shader);
		return false;
	}

	glslang_program_t * program = glslang_program_create();
	glslang_program_add_shader(program, shader);
	ok = glslang_program_link(program, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT | GLSLANG_MSG_READ_HLSL_BIT);
	if (ok) {
		glslang_program_SPIRV_generate(program, stage);
		const size_t words = glslang_program_SPIRV_get_size(program);
		const unsigned char * bytes = reinterpret_cast<const unsigned char *>(glslang_program_SPIRV_get_ptr(program));
		spirv.assign(bytes, bytes + words * 4);
		const char * messages = glslang_program_SPIRV_get_messages(program);
		if (messages != NULL) log = messages;
		ok = !spirv.empty();
	}
	else {
		log = glslang_program_get_info_log(program);
	}
	glslang_program_delete(program);
	glslang_shader_delete(shader);
	return ok;
}

bool SDL3_Translate_SPIRV_To_MSL(const std::vector<unsigned char> & spirv, bool vertex_stage, std::string & msl,
	std::string & log)
{return SDL3_Translate_MSL(spirv,vertex_stage?SdlShaderStage::Vertex:SdlShaderStage::Fragment,msl,log);}

bool SDL3_Translate_MSL(const std::vector<unsigned char> &spirv,SdlShaderStage stage,std::string &msl,std::string &log)
{
	start_shadercross();
	msl.clear();
	log.clear();
	if (!shadercross_ok) {
		log = SDL_GetError();
		return false;
	}
	const SDL_ShaderCross_SPIRV_Info info = spirv_info(spirv, stage);
	char * text = static_cast<char *>(SDL_ShaderCross_TranspileMSLFromSPIRV(&info));
	if (text == NULL) {
		log = SDL_GetError();
		return false;
	}
	msl = text;
	SDL_free(text);
	return true;
}

void SDL3_Shader_Slots(const std::vector<unsigned char> & spirv, bool vertex_stage, unsigned & samplers,
	unsigned & uniform_buffers)
{
	// SDL's sets: a vertex program's textures in 0 and constant buffers in 1, a pixel program's in 2
	// and 3 (SDL_CreateGPUShader).  Read straight from the OpDecorate instructions: the reflection
	// SDL_shadercross offers counts what is used, and the count is what SDL cannot take.
	const unsigned texture_set = vertex_stage ? 0 : 2;
	const unsigned uniform_set = vertex_stage ? 1 : 3;
	const size_t words = spirv.size() / 4;
	std::vector<unsigned> set_of(0), binding_of(0);
	const unsigned char * bytes = spirv.empty() ? NULL : &spirv[0];
	unsigned id_bound = words > 3 ? (unsigned)(bytes[12] | (bytes[13] << 8) | (bytes[14] << 16) | ((unsigned)bytes[15] << 24)) : 0;
	set_of.assign(id_bound, ~0u);
	binding_of.assign(id_bound, ~0u);
	for (size_t at = 5; at < words;) {
		const unsigned char * w = bytes + at * 4;
		const unsigned word = w[0] | (w[1] << 8) | (w[2] << 16) | ((unsigned)w[3] << 24);
		const unsigned opcode = word & 0xffff, length = word >> 16;
		if (length == 0) break;
		if (opcode == 71 && length >= 4 && at + 3 < words) {	// OpDecorate target decoration value
			const unsigned char * a = bytes + (at + 1) * 4;
			const unsigned char * b = bytes + (at + 2) * 4;
			const unsigned char * c = bytes + (at + 3) * 4;
			const unsigned target = a[0] | (a[1] << 8) | (a[2] << 16) | ((unsigned)a[3] << 24);
			const unsigned decoration = b[0] | (b[1] << 8) | (b[2] << 16) | ((unsigned)b[3] << 24);
			const unsigned value = c[0] | (c[1] << 8) | (c[2] << 16) | ((unsigned)c[3] << 24);
			if (target < id_bound && decoration == 34) set_of[target] = value;		// DescriptorSet
			if (target < id_bound && decoration == 33) binding_of[target] = value;	// Binding
		}
		at += length;
	}
	samplers = 0;
	uniform_buffers = 0;
	for (unsigned id = 0; id < id_bound; ++id) {
		if (binding_of[id] == ~0u) continue;
		if (set_of[id] == texture_set && binding_of[id] + 1 > samplers) samplers = binding_of[id] + 1;
		if (set_of[id] == uniform_set && binding_of[id] + 1 > uniform_buffers) uniform_buffers = binding_of[id] + 1;
	}
}

SDL_GPUShader * SDL3_Create_Shader(SDL_GPUDevice * device, const std::vector<unsigned char> & spirv,
	bool vertex_stage, std::string & log)
{
	start_shadercross();
	log.clear();
	if (!shadercross_ok) {
		log = SDL_GetError();
		return NULL;
	}
	// Counted from the SPIR-V itself.  A program reading textures at slots 0 and 7 needs eight, and SDL
	// binds slots 0 to n - 1.  shadercross's reflection counts what is used (two), and sorts a texture
	// read only with .Load into the storage textures, so neither its count nor its split is trusted.
	SdlShaderResources counted;
	SDL3_Shader_Resources(spirv, vertex_stage, counted);
	SDL_ClearError();

	// Metal: shadercross's own creation path does not take the counts it is given (the Metal soft
	// particle depth texture, at slot 7, was created as a two-slot program and left unbound).  Make the
	// MSL the same way it does and hand SDL the counts ourselves.  The entry point is main0 (MSL renames main).
	const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device);
	if (!(formats & SDL_GPU_SHADERFORMAT_SPIRV) && (formats & SDL_GPU_SHADERFORMAT_MSL)) {
		std::string msl, translate_log;
		if (!SDL3_Translate_MSL(spirv, vertex_stage ? SdlShaderStage::Vertex : SdlShaderStage::Fragment, msl, translate_log)) {
			log = translate_log.empty() ? "SPIR-V to MSL failed without a reason" : translate_log;
			return NULL;
		}
		SDL_GPUShaderCreateInfo create;
		SDL_zero(create);
		create.code = reinterpret_cast<const Uint8 *>(msl.c_str());
		create.code_size = msl.size() + 1;
		create.entrypoint = "main0";
		create.format = SDL_GPU_SHADERFORMAT_MSL;
		create.stage = vertex_stage ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
		create.num_samplers = counted.samplers;
		create.num_storage_textures = counted.storage_textures;
		create.num_storage_buffers = counted.storage_buffers;
		create.num_uniform_buffers = counted.uniform_buffers;
		SDL_ClearError();
		SDL_GPUShader * metal_shader = SDL_CreateGPUShader(device, &create);
		if (metal_shader == NULL) {
			log = SDL_GetError();
			if (log.empty()) log = "Metal refused the program, and SDL did not say why";
		}
		return metal_shader;
	}

	const SDL_ShaderCross_SPIRV_Info info = spirv_info(spirv, vertex_stage?SdlShaderStage::Vertex:SdlShaderStage::Fragment);
	SDL_ShaderCross_GraphicsShaderResourceInfo resources;
	SDL_zero(resources);
	resources.num_samplers = counted.samplers;
	resources.num_uniform_buffers = counted.uniform_buffers;
	resources.num_storage_textures = counted.storage_textures;
	resources.num_storage_buffers = counted.storage_buffers;
	// shadercross leaves an error from its start-up (it looks for libraries it may not need) where a
	// later failure that sets none would report it.  Cleared, so a refusal says its own reason.
	SDL_ClearError();
	SDL_GPUShader * shader = SDL_ShaderCross_CompileGraphicsShaderFromSPIRV(device, &info, &resources, 0);
	if (shader == NULL) {
		log = SDL_GetError();
		if (log.empty()) log = "refused, and neither SDL nor SDL_shadercross said why";
	}
	return shader;
}

SDL_GPUComputePipeline *SDL3_Create_Compute(SDL_GPUDevice *device,const std::vector<unsigned char> &spirv,std::string &log)
{
    start_shadercross();log.clear();if(!shadercross_ok){log=SDL_GetError();return nullptr;}
    const auto info=spirv_info(spirv,SdlShaderStage::Compute);
    auto *resources=SDL_ShaderCross_ReflectComputeSPIRV(spirv.data(),spirv.size(),0);
    if(!resources){log=SDL_GetError();return nullptr;}
    auto *pipeline=SDL_ShaderCross_CompileComputePipelineFromSPIRV(device,&info,resources,0);
    SDL_free(resources);if(!pipeline)log=SDL_GetError();return pipeline;
}
