#version 450
// A sun-shadow caster (engine improvements 2.0 step 2): instead of the title's alpha-only silhouette, its depth
// along the sun (packed, sun_common.glsl) into the shadow texture; our depth buffer keeps the nearest caster.
// Cut-out materials keep their holes through the title's alpha test / alpha kill on the base texture.
#extension GL_GOOGLE_include_directive : require
#include "sun_common.glsl"
void main() {
    if (ps.alpha_test.x != 7.0 || ps.alpha_kill.x != 0.0) {
        const float a = textureProj(sampler2D(tex0, smp0), t0 * ps.texcoord_scale[0]).a;
        if (ps.alpha_kill.x != 0.0 && a < 1.0 / 512.0) discard;
        alpha_test(a);
    }
    color = vec4(pack_depth(1.0 - gl_FragCoord.z), 1.0);
}
