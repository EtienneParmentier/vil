#version 450 core

layout(location = 0) in struct {
    vec4 color;
    vec2 uv;
} In;

layout(location = 0) out vec4 fragColor;

const uint flagMaskR = (1u << 0u);
const uint flagMaskG = (1u << 1u);
const uint flagMaskB = (1u << 2u);
const uint flagMaskA = (1u << 3u);
const uint flagGrayscale = (1u << 4u);

layout(push_constant) uniform PCR {
	layout(offset = 16) float layer;
	layout(offset = 20) float valMin;
	layout(offset = 24) float valMax;
	layout(offset = 28) uint flags;
} pcr;

#ifdef TEX_TYPE_1D_ARRAY
	layout(set = 0, binding = 0) uniform sampler1DArray sTexture;

	vec4 sampleTex() {
		return texture(sTexture, vec2(In.uv.x, pcr.layer));
	}
#elif defined(TEX_TYPE_2D_ARRAY)
	layout(set = 0, binding = 0) uniform sampler2DArray sTexture;

	vec4 sampleTex() {
		return texture(sTexture, vec3(In.uv.xy, pcr.layer));
	}
// #elif TEX_TYPE_CUBE
// TODO: cubearray?
// layout(set = 0, binding = 0) uniform samplerCube sTexture;
#elif defined(TEX_TYPE_3D)
	layout(set = 0, binding = 0) uniform sampler3D sTexture;

	vec4 sampleTex() {
		return texture(sTexture, vec3(In.uv.xy, pcr.layer));
	}
#endif

vec4 remap(vec4 val, float oldLow, float oldHigh, float newLow, float newHigh) {
	vec4 t = (val - oldLow) / (oldHigh - oldLow);
	return mix(vec4(newLow), vec4(newHigh), t);
}

void main() {
	vec4 texCol = remap(sampleTex(), pcr.valMin, pcr.valMax, 0, 1);

	texCol.r *= float((pcr.flags & flagMaskR) != 0);
	texCol.g *= float((pcr.flags & flagMaskG) != 0);
	texCol.b *= float((pcr.flags & flagMaskB) != 0);
	texCol.a = (pcr.flags & flagMaskA) != 0 ? texCol.a : 1.f;

	// TODO: add additional luminance mode? might be what some people
	// expect from grayscale
	if((pcr.flags & flagGrayscale) != 0) {
		texCol = vec4(dot(texCol.rgb, 1.f.xxx));
	}

    fragColor = In.color * texCol;
}
