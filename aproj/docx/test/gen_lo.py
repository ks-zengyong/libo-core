"""
生成 LibreOffice 侧的 Frame 树记录和 VCL 渲染指令记录。

通过启动 soffice --headless --convert-to pdf 触发完全排版，生成:
  - lo_frame.txt  (Frame 层语义指令，由 SW_RENDER_LOG 环境变量控制)
  - lo_vcl.txt    (VCL 层绘制指令，由 SW_VCL_RENDER_LOG 环境变量控制)

用法:
  cd aproj/docx
  python test/gen_lo.py [sample.docx]

  若不指定 sample.docx，默认使用 samples/sample0.docx。

依赖:
  - LibreOffice 已编译 (libo-core/instdir/program/soffice.exe)
  - SW_RENDER_LOG / SW_VCL_RENDER_LOG 支持 (libo-core/sw/ 中已实现 SwPaintEventListener)
"""

import os
import sys
import subprocess
import time
import tempfile
from pathlib import Path


def find_script_dir():
    """返回脚本所在目录 (test/)。"""
    return Path(__file__).resolve().parent


def find_libo_root():
    """从脚本目录向上找到 libo-core 根目录。

    test/gen_lo.py → aproj/docx/test/ → aproj/docx/ → aproj/ → libo-core/
    """
    script_dir = find_script_dir()
    return script_dir.parent.parent.parent


def find_soffice(libo_root):
    """查找 LibreOffice soffice.exe（相对于 libo-core 根目录）。"""
    candidates = [
        libo_root / "instdir" / "program" / "soffice.exe",
        libo_root / "instdir" / "program" / "soffice.bin",
    ]
    for c in candidates:
        if c.exists():
            return str(c)
    return None


def kill_existing_soffice():
    """终止已有的 soffice 进程。"""
    try:
        subprocess.run(
            ["taskkill", "/f", "/im", "soffice.exe"],
            capture_output=True, timeout=5
        )
        time.sleep(2)
    except Exception:
        pass
    try:
        subprocess.run(
            ["taskkill", "/f", "/im", "soffice.bin"],
            capture_output=True, timeout=5
        )
        time.sleep(1)
    except Exception:
        pass


def main():
    script_dir = find_script_dir()
    libo_root = find_libo_root()

    # 产物输出到脚本所在目录 (test/)
    test_dir = script_dir

    soffice = find_soffice(libo_root)
    if not soffice:
        print("[ERROR] 找不到 soffice.exe")
        print(f"  请确认 LibreOffice 已编译，soffice.exe 位于: libo-core/instdir/program/")
        print(f"  libo_root = {libo_root}")
        sys.exit(1)

    # 确定输入文件 (相对于 aproj/docx/)
    docx_root = script_dir.parent
    if len(sys.argv) >= 2:
        input_docx = sys.argv[1]
    else:
        input_docx = str(docx_root / "samples" / "sample0.docx")

    if not os.path.exists(input_docx):
        print(f"[ERROR] 输入文件不存在: {input_docx}")
        sys.exit(1)

    input_docx = os.path.abspath(input_docx)

    print(f"[INFO] libo-core:   {libo_root}")
    print(f"[INFO] soffice:     {soffice}")
    print(f"[INFO] 输入文件:    {input_docx}")

    # 终止已有 soffice
    kill_existing_soffice()

    # 产物路径
    lo_frame = str(test_dir / "lo_frame.txt")
    lo_vcl = str(test_dir / "lo_vcl.txt")

    env = os.environ.copy()
    env["SW_RENDER_LOG"] = lo_frame     # Frame 层 → lo_frame.txt
    env["SW_VCL_RENDER_LOG"] = lo_vcl   # VCL 层  → lo_vcl.txt

    # 使用 --convert-to pdf 触发完全排版
    # PDF 转换会强制 LibreOffice 渲染所有页面，确保 SwPaintEventListener 捕获完整指令
    with tempfile.TemporaryDirectory() as tmpdir:
        cmd = [
            soffice,
            "--headless",
            "--norestore",
            "--nologo",
            "--convert-to", "pdf",
            "--outdir", tmpdir,
            input_docx,
        ]
        print(f"[INFO] 执行: {' '.join(cmd)}")
        print(f"[INFO] SW_RENDER_LOG={lo_frame}")
        print(f"[INFO] SW_VCL_RENDER_LOG={lo_vcl}")
        print("[INFO] 等待 soffice 完全排版并输出 (最长 120s)...")

        try:
            proc = subprocess.run(
                cmd,
                cwd=os.path.dirname(soffice),
                env=env,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                timeout=120,
            )
        except subprocess.TimeoutExpired:
            print("[ERROR] soffice 超时 (120s)，可能文档加载失败或 LibreOffice 无响应")
            kill_existing_soffice()
            sys.exit(1)

        if proc.stdout:
            print("[STDOUT]", proc.stdout[:500])
        if proc.stderr:
            print("[STDERR]", proc.stderr[:500])

    # 检查产物
    frame_ok = os.path.exists(lo_frame)
    vcl_ok = os.path.exists(lo_vcl)

    if frame_ok:
        print(f"[OK]   lo_frame.txt: {os.path.getsize(lo_frame)} bytes")
    else:
        print("[MISS] lo_frame.txt")

    if vcl_ok:
        print(f"[OK]   lo_vcl.txt:   {os.path.getsize(lo_vcl)} bytes")
    else:
        print("[MISS] lo_vcl.txt")

    if not frame_ok and not vcl_ok:
        print("[ERROR] 未生成任何产物，请确认 libo-core 已包含 SwPaintEventListener 支持")
        sys.exit(1)

    print(f"\n[INFO] 产物目录: {test_dir}")


if __name__ == "__main__":
    main()