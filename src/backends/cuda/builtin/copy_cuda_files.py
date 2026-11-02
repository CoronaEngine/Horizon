
import os
import shutil
import glob
import sys

def copy_file(src_dir, src_file, dst_dir):
    filename = os.path.basename(src_file)
    fn = os.path.relpath(src_file,src_dir);
    # print(fn)
    dst_file = os.path.join(dst_dir, fn)

    if os.path.exists(dst_file):
        src_time = os.path.getmtime(src_file)
        dst_time = os.path.getmtime(dst_file)
        if src_time > dst_time:
            dst_d = os.path.dirname(dst_file)
            if not os.path.exists(dst_d):
                os.makedirs(dst_d)
                print(f"create dir: {dst_d}")
            shutil.copy2(src_file, dst_file)
    else:
        dst_d = os.path.dirname(dst_file)
        if not os.path.exists(dst_d):
            os.makedirs(dst_d)
            print(f"create dir: {dst_d}")
        shutil.copy2(src_file, dst_file)
        
        
def copy_files(src_dir, dst_dir):
    os.makedirs(dst_dir, exist_ok=True)

    for src_file in glob.glob(os.path.join(src_dir, '**/*.h'), recursive=True):
        copy_file(src_dir, src_file, dst_dir)



args = sys.argv

dst = ""

if len(args) > 1:
    dst = args[1]
else:
    dst = "E:/work/compile/Vision/cmake-build-debug/bin/cuda"
    
print("dst is ",dst)

def get_module_path():
    return os.path.dirname(os.path.abspath(__file__))

src = get_module_path()
os.chdir(src)

copy_files(src, dst)