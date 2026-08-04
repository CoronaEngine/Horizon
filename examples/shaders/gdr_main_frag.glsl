#version 450

layout(set = 3, binding = 1) uniform Materials
{
    vec4 color0;
    vec4 color1;
    vec4 color2;
    vec4 color3;
    vec4 color4;
    vec4 color5;
    vec4 color6;
    vec4 color7;
    vec4 color8;
    vec4 color9;
    vec4 color10;
    vec4 color11;
    vec4 color12;
    vec4 color13;
    vec4 color14;
    vec4 color15;
} materials;

vec4 material_color(int id)
{
    switch (id)
    {
    default: return materials.color0;
    case 0: return materials.color0;
    case 1: return materials.color1;
    case 2: return materials.color2;
    case 3: return materials.color3;
    case 4: return materials.color4;
    case 5: return materials.color5;
    case 6: return materials.color6;
    case 7: return materials.color7;
    case 8: return materials.color8;
    case 9: return materials.color9;
    case 10: return materials.color10;
    case 11: return materials.color11;
    case 12: return materials.color12;
    case 13: return materials.color13;
    case 14: return materials.color14;
    case 15: return materials.color15;
    }
}

layout(location = 0) flat in int v_materialID;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 color = material_color(v_materialID);

    // Dithered transparency for walls (alpha < 1), matching bgfx.
    if (color.a < 0.999)
    {
        ivec2 p = ivec2(gl_FragCoord.xy);
        if (((p.x + p.y) & 1) == 0)
            discard;
    }

    outColor = vec4(color.rgb, 1.0);
}
