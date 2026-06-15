"""
生成 aproj/docx 侧的 Frame 树记录和 VCL 渲染指令记录。

通过调用 docx_e2e_test.exe 编译产物，生成:
  - test/aproj_frame.txt  (Frame 层语义指令)
  - test/aproj_vcl.txt    (VCL 层绘制指令)

用法:
  cd aproj/docx
  python test/gen_aproj.py [sample.docx]

  若不指定 sample.docx，默认使用 samples/sample0.docx。
"""

import os
import sys
import subprocess
import glob
import shutil
from pathlib import Path


def find_project_root():
    """从脚本所在目录向上查找 aproj/docx 工程根目录。"""
    script_dir = Path(__file__).resolve().parent
    # test/ -> aproj/docx/
    return script_dir.parent


def find_exe(proj_root):
    """查找 docx_e2e_test.exe 编译产物。"""
    # 优先查找 output 目录下的 debug 产物
    candidates = [
        proj_root / "output" / "docx_e2e_test_debug.exe",
        proj_root / "output" / "docx_e2e_test.exe",
        proj_root / "build" / "Debug" / "docx_e2e_test_debug.exe",
        proj_root / "build" / "Debug" / "docx_e2e_test.exe",
        proj_root / "build" / "Release" / "docx_e2e_test.exe",
        proj_root / "build" / "RelWithDebInfo" / "docx_e2e_test.exe",
        proj_root / "build" / "MinSizeRel" / "docx_e2e_test.exe",
    ]
    for c in candidates:
        if c.exists():
            return str(c)
    # 递归搜索
    for exe in proj_root.rglob("docx_e2e_test*.exe"):
        return str(exe)
    return None


def main():
    proj_root = find_project_root()
    test_dir = proj_root / "test"
    test_dir.mkdir(exist_ok=True)

    exe = find_exe(proj_root)
    if not exe:
        print("[ERROR] 找不到 docx_e2e_test.exe，请先编译项目:")
        print("  cd aproj/docx && cmake -B build && cmake --build build --config Debug")
        sys.exit(1)

    # 确定输入文件
    if len(sys.argv) >= 2:
        input_docx = sys.argv[1]
    else:
        input_docx = str(proj_root / "samples" / "sample0.docx")

    if not os.path.exists(input_docx):
        print(f"[ERROR] 输入文件不存在: {input_docx}")
        sys.exit(1)

    print(f"[INFO] 工程根目录: {proj_root}")
    print(f"[INFO] 可执行文件: {exe}")
    print(f"[INFO] 输入文件:   {input_docx}")

    # 运行 e2e test
    env = os.environ.copy()
    cmd = [exe, input_docx]
    print(f"[INFO] 执行: {' '.join(cmd)}")
    proc = subprocess.run(
        cmd,
        cwd=str(proj_root),
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )

    print(proc.stdout)
    if proc.stderr:
        print("[STDERR]", proc.stderr[:2000])

    if proc.returncode != 0:
        print(f"[WARN] e2e_test 返回码: {proc.returncode}")
        print("[WARN] 产物可能不完整，但将继续收集已生成的文件")

    # 收集产物
    # e2e_test 在 cwd (proj_root) 下运行，产物写入 test/ 目录（相对路径）
    expected_files = {
        "aproj_frame.txt": test_dir / "aproj_frame.txt",
        "aproj_vcl.txt":   test_dir / "aproj_vcl.txt",
    }

    found = 0
    for name, path in expected_files.items():
        if path.exists():
            size = path.stat().st_size
            print(f"[OK]   {name} ({size} bytes)")
            found += 1
        else:
            print(f"[MISS] {name}")

    print(f"\n[INFO] 共生成 {found}/{len(expected_files)} 个产物文件")
    print(f"[INFO] 产物目录: {test_dir}")

    if found == 0:
        sys.exit(1)


if __name__ == "__main__":
    main()