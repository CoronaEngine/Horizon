#version 450

vec3 boxAlbedo(vec3 normal) {
    vec3 face = abs(normal);
    return face.x * vec3(0.18, 0.54, 0.86)
         + face.y * vec3(0.98, 0.58, 0.20)
         + face.z * vec3(0.18, 0.73, 0.56);
}
