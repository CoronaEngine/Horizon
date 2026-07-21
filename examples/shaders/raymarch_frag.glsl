#version 450

// 移植自参考示例 03-raymarch 的 fs_raymarching.sc + iq_sdf.sh：
// 球体追踪（sphere tracing）渲染圆角盒 + 6 球的 SDF 场景。
// inv_mvp 把裁剪空间 (uv, z, 1) 反投影回模型空间得到光线起终点。

layout(binding = 0) uniform RaymarchParams {
    mat4 inv_mvp;
    vec4 light_dir_time; // xyz: 模型空间光方向, w: time
} rmp;

layout(location = 0) in vec3 v_color;
layout(location = 1) in vec2 v_texcoord;

layout(location = 0) out vec4 outColor;

float sdSphere(vec3 p, float s)
{
    return length(p) - s;
}

float udRoundBox(vec3 p, vec3 b, float r)
{
    return length(max(abs(p) - b, 0.0)) - r;
}

float sceneDist(vec3 pos)
{
    float d1 = udRoundBox(pos, vec3(2.5, 2.5, 2.5), 0.5);
    float d2 = sdSphere(pos + vec3(4.0, 0.0, 0.0), 1.0);
    float d3 = sdSphere(pos + vec3(-4.0, 0.0, 0.0), 1.0);
    float d4 = sdSphere(pos + vec3(0.0, 4.0, 0.0), 1.0);
    float d5 = sdSphere(pos + vec3(0.0, -4.0, 0.0), 1.0);
    float d6 = sdSphere(pos + vec3(0.0, 0.0, 4.0), 1.0);
    float d7 = sdSphere(pos + vec3(0.0, 0.0, -4.0), 1.0);
    return min(min(min(min(min(min(d1, d2), d3), d4), d5), d6), d7);
}

vec3 calcNormal(vec3 pos)
{
    const vec2 delta = vec2(0.002, 0.0);
    float nx = sceneDist(pos + delta.xyy) - sceneDist(pos - delta.xyy);
    float ny = sceneDist(pos + delta.yxy) - sceneDist(pos - delta.yxy);
    float nz = sceneDist(pos + delta.yyx) - sceneDist(pos - delta.yyx);
    return normalize(vec3(nx, ny, nz));
}

float calcAmbOcc(vec3 pos, vec3 normal)
{
    float occ = 0.0;
    float aostep = 0.2;
    for (int ii = 1; ii < 4; ii++)
    {
        float fi = float(ii);
        float dist = sceneDist(pos + normal * fi * aostep);
        occ += (fi * aostep - dist) / pow(2.0, fi);
    }
    return 1.0 - occ;
}

float trace(vec3 ray, vec3 dir, float maxd)
{
    float tt = 0.0;
    float epsilon = 0.001;
    for (int ii = 0; ii < 64; ii++)
    {
        float dist = sceneDist(ray + dir * tt);
        if (dist > epsilon)
        {
            tt += dist;
        }
    }
    return tt < maxd ? tt : 0.0;
}

vec2 blinn(vec3 lightDir, vec3 normal, vec3 viewDir)
{
    float ndotl = dot(normal, lightDir);
    vec3 reflected = lightDir - 2.0 * ndotl * normal;
    float rdotv = dot(reflected, viewDir);
    return vec2(ndotl, rdotv);
}

float fresnel(float ndotl, float bias, float pw)
{
    float facing = (1.0 - ndotl);
    return max(bias + (1.0 - bias) * pow(facing, pw), 0.0);
}

vec4 lit(float ndotl, float rdotv, float m)
{
    float diff = max(0.0, ndotl);
    float spec = step(0.0, ndotl) * max(0.0, rdotv * m);
    return vec4(1.0, diff, spec, 1.0);
}

void main()
{
    vec4 tmp;
    tmp = rmp.inv_mvp * vec4(v_texcoord.xy, 0.0, 1.0);
    vec3 eye = tmp.xyz / tmp.w;

    tmp = rmp.inv_mvp * vec4(v_texcoord.xy, 1.0, 1.0);
    vec3 at = tmp.xyz / tmp.w;

    float maxd = length(at - eye);
    vec3 dir = normalize(at - eye);

    float dist = trace(eye, dir, maxd);

    if (dist > 0.5)
    {
        vec3 pos = eye + dir * dist;
        vec3 normal = calcNormal(pos);

        vec2 bln = blinn(rmp.light_dir_time.xyz, normal, dir);
        vec4 lc = lit(bln.x, bln.y, 1.0);
        float fres = fresnel(bln.x, 0.2, 5.0);

        float val = 0.9 * lc.y + pow(lc.z, 128.0) * fres;
        val *= calcAmbOcc(pos, normal);
        val = pow(val, 1.0 / 2.2);

        outColor = vec4(val, val, val, 1.0);
        gl_FragDepth = dist / maxd;
    }
    else
    {
        outColor = vec4(v_color, 1.0);
        gl_FragDepth = 1.0;
    }
}
