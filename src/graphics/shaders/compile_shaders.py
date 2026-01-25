# compile_shaders.py
import os
import subprocess
import glob

def get_shader_stage(filename):
    """Infer shader stage from filename."""
    name = os.path.splitext(filename.lower())[0]
    if "vert" in name:
        return "vertex"
    elif "frag" in name:
        return "fragment"
    elif "comp" in name:
        return "compute"
    elif "geom" in name:
        return "geometry"
    elif "tesc" in name:
        return "tesscontrol"
    elif "tese" in name:
        return "tessevaluation"
    else:
        print(f"Warning: Could not infer stage for {filename}, defaulting to vertex")
        return "vertex"

def compile_shader(input_file, output_file):
    if not os.path.exists(input_file):
        print(f"Error: {input_file} not found")
        return False
    
    stage = get_shader_stage(input_file)
    cmd = ["glslc", "-fshader-stage=" + stage, input_file, "-o", output_file]
    result = subprocess.run(cmd, capture_output=True, text=True)
    
    if result.returncode != 0:
        print(f"Failed to compile {input_file}: {result.stderr}")
        return False
    
    print(f"Compiled {input_file} to {output_file} as {stage} shader")
    return True

def delete_spv_files(directory):
    """Delete all .spv files in the specified directory."""
    spv_files = glob.glob(os.path.join(directory, "*.spv"))
    if not spv_files:
        print("No .spv files found to delete")
    else:
        for spv_file in spv_files:
            try:
                os.remove(spv_file)
                print(f"Deleted {spv_file}")
            except OSError as e:
                print(f"Error deleting {spv_file}: {e}")

if __name__ == "__main__":
    # Change to script's directory
    script_dir = os.path.dirname(os.path.abspath(__file__))
    os.chdir(script_dir)
    print(f"Working directory set to: {script_dir}")

    # Delete all existing .spv files
    delete_spv_files(script_dir)

    # Find all .glsl files
    glsl_files = glob.glob("*.glsl")
    if not glsl_files:
        print("No .glsl files found in directory")
        exit(1)

    success = True
    for glsl_file in glsl_files:
        output_file = os.path.splitext(glsl_file)[0] + ".spv"
        if not compile_shader(glsl_file, output_file):
            success = False

    if not success:
        exit(1)