import os
import subprocess
import pathlib

if __name__ == '__main__':
    glslang_cmd = "glslangValidator"

    shader_list = [
        "simple.vert",
        "simple.frag",
        "debug_points.vert",
        "debug_points.frag",
        "GenSamples.comp",
        "ComputeFF.comp",
        "initLighting.comp",
        "oneBounce.comp",
        "aliasBounce.comp",
        "CorrectFF.comp",
        "debug_lines.vert",
        "debug_lines.frag",
        "FinalLighting.comp",
        "packFF.comp",
        "debug_cubes.vert",
        "debug_cubes.frag",
    ]

    for shader in shader_list:
        subprocess.run([glslang_cmd, "-V", shader, "-o", "{}.spv".format(shader)])

