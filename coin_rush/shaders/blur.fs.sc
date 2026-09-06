$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);  // scene color

uniform vec4 u_blur;  // xy=texel size (1/res), z=radius

void main()
{
	vec2 texel = u_blur.xy * u_blur.z;
	vec4 sum = vec4(0.0, 0.0, 0.0, 0.0);
	for (int y = -2; y <= 2; ++y)
	{
		for (int x = -2; x <= 2; ++x)
		{
			sum += texture2D(s_texColor,
			                 v_texcoord0 + vec2(float(x), float(y)) * texel);
		}
	}
	gl_FragColor = sum / 25.0;
}
