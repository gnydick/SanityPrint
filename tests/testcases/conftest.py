from __future__ import annotations
import os
import time
import sys
import subprocess
import json
from pathlib import Path

import git
import pytest
import pyautogui
from pyautogui import ImageNotFoundException

#================================================
#                   fixture
#================================================
# CREALITY_PRINTER_PATH
# CREALITY_PRINTER_LOG_PATH
# CREALITY_PRINTER_THEME

def sanity_printer_path():
    """return creality printer path.
    """
    path = os.getenv("CREALITY_PRINTER_PATH")
    if not path:
        pytest.fail("CREALITY_PRINTER_PATH not set! Please set it first!")
    if not os.path.exists(path):
        pytest.fail(f"software not exist: {path}")
    return path

def log_path():
    path = os.getenv("CREALITY_PRINTER_LOG_PATH")
    if not path:
        pytest.fail("CREALITY_PRINTER_LOG_PATH not set! Please set it first!")
    if not os.path.exists(path):
        pytest.fail(f"log path not exist: {path}")
    return os.path.join(path, "creality.tlog")

@pytest.fixture(scope="session", autouse=True)
def test_resources_path(pytestconfig):
    """pytest resources path
    """
    rootdir = pytestconfig.rootdir
    return os.path.join(rootdir, "_resources_")

############ Sanity Printer操作 #########
@pytest.fixture(scope="session")
def open_sanity_printer():
    """Open and close creality printer.
    """
    if os.path.exists(log_path()):
        os.remove(log_path())
    if os.path.exists(f"{log_path()}.offset"):
        os.remove(f"{log_path()}.offset")        
    proc = subprocess.Popen(
        [sanity_printer_path(), "test#157369"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    app_respone("APP", "READY", 0)
    yield
    proc.terminate()

@pytest.fixture(scope="package")
def max_windows(test_resources_path):
    """
    max creality printer window
    """
    
    btn_paths = [
        fr"{test_resources_path}\max_btn.png",
        fr"{test_resources_path}\max_btn.png",
    ]

    btn_location = None
    found_count = -1
    for count, btn_path in enumerate(btn_paths, start=1):
        try:
            btn_location = pyautogui.locateOnScreen(btn_path, confidence=0.7)
            if btn_location is not None:
                found_count = count
                break
        except ImageNotFoundException:
            continue

    if btn_location is None:
        raise FileNotFoundError("Neither max_btn.png nor min_btn.png was found on screen!")

    if found_count == 1:
        max_btn_center = pyautogui.center(btn_location)
        pyautogui.click(max_btn_center)
        time.sleep(0.3)
    else:
        # had been maximized window
        pass


############ log #########
def read_new_log_lines(log_path):
    offset_file = f"{log_path}.offset"
    if not os.path.exists(log_path):
        return None
    
    current_size = os.path.getsize(log_path)
    
    if os.path.exists(offset_file):
        with open(offset_file, 'r') as f:
            try:
                offset = int(f.read().strip())
            except ValueError:
                offset = 0
    else:
        offset = 0
    
    if offset == current_size:
        return None
    
    with open(log_path, 'r', encoding='utf-8') as f:
        f.seek(offset)
        new_content = f.read()
    
    with open(offset_file, 'w') as f:
        f.write(str(current_size))

    return new_content

def app_respone(m, f, c, timeout=-1):
    def ret(code, error, match_content):
        return {
            "code": code,
            "error": error,
            "data": match_content
        }
    
    def match_keyword(m, f, c):
        return f"{m}_{f}_{c}"
        
    start_time = time.perf_counter()
    while True:
        try:
            lines = read_new_log_lines(log_path())
            if not lines is None:
                for line in lines.split("\n"):
                    if match_keyword(m, f, c) in line.strip():
                        return ret(200, None, line.strip())
        except FileNotFoundError as e:
            pass
            
        if timeout != -1:
            if (time.perf_counter() - start_time) >= timeout:
                return ret(500, "timeout", None)
        
        time.sleep(0.1) # 100ms 防cpu占用

@pytest.fixture()
def check_app_respone():
    return app_respone

#================================================
#               fixture end
#================================================





############ paramlized function #########
def get_module_rel_path(rootdir, script_path):
    script_dir = Path(os.path.dirname(script_path))
    scenario_dir = Path(rootdir)
    relative_path = script_dir.relative_to(scenario_dir)
    return relative_path

def parametrize(metafunc, setting_map):
    test_module = metafunc.module.__name__.split(".")[-1]
    rel_path_origin = get_module_rel_path(metafunc.config.rootdir, metafunc.module.__file__)
    rel_path = str(rel_path_origin).replace(os.sep, ".")
    
    # 默认注入
    def default_parametrize():
        default_params = {
            "baseline_cur_path": os.path.join(baseline_local(), rel_path_origin)
        }
        for param in default_params:
            if param in metafunc.fixturenames:
                metafunc.parametrize(param, [default_params[param]])

    # 表格注入
    def table_paramerize():
        if test_module in setting_map.keys():
            for fixture_name in setting_map[test_module].keys():
                if fixture_name in metafunc.fixturenames:
                    baseline_key = setting_map[test_module][fixture_name]
                    baseline_data = metafunc.config.baseline_data
                    metafunc.parametrize(fixture_name, baseline_data[rel_path][test_module][baseline_key])

    default_parametrize()
    table_paramerize()

##################pytest_configure##################
def baseline_url():
    return "http://172.20.180.12:8050/C3DBaseline"

def baseline_local() -> str:
    def appdata_path():
        # --- Windows 系统 ---
        if sys.platform == 'win32':
            appdata_path = os.getenv('APPDATA')
            if not appdata_path:
                raise EnvironmentError("Windows 环境变量 %APPDATA% 未找到！")
            return appdata_path
        
        # --- Linux/Mac 系统 ---
        else:
            # 尝试获取 $XDG_DATA_HOME (Linux 标准)
            xdg_data_home = os.getenv('XDG_DATA_HOME')
            if xdg_data_home:
                return xdg_data_home
            
            # Mac 的默认路径 (~/Library/Application Support)
            if sys.platform == 'darwin':
                return os.path.expanduser('~/Library/Application Support')
            
            # Linux 的默认路径 (~/.local/share)
            return os.path.expanduser('~/.local/share')
    return fr"{appdata_path()}/C3DBaseline"
    # return fr"D:\WORK-REPOS\C3DBaseline"

def update_baseline():
    """update baseline data
    """
    """
    Fixture 作用：
    1. 检查本地是否存在目标 Git 仓库。
    2. 如果存在，拉取最新代码（同步到最新状态）。
    3. 如果不存在，克隆仓库。
    4. 如果操作失败，抛出明确的错误信息。
    """
    repo_url = baseline_url()
    local_path = baseline_local()
    
    # --- 1. 检查本地仓库是否存在 ---
    if os.path.exists(local_path):
        print(f"\n本地仓库已存在: {local_path}，尝试拉取最新代码...")
        try:
            # 打开现有仓库并拉取更新
            repo = git.Repo(local_path)
            repo.remotes.origin.pull()  # 拉取远程最新代码
            print("仓库同步成功！")
            return local_path  # 返回本地路径供测试使用
        except git.GitCommandError as e:
            pytest.fail(f"拉取仓库失败: {e}")  # 拉取失败时终止测试
    else:
        # --- 2. 本地仓库不存在，尝试克隆 ---
        print(f"\n本地仓库不存在: {local_path}，尝试克隆...")
        try:
            git.Repo.clone_from(repo_url, local_path)  # 克隆仓库
            print("仓库克隆成功！")
            return local_path  # 返回本地路径供测试使用
        except git.GitCommandError as e:
            pytest.fail(f"克隆仓库失败: {e}")  # 克隆失败时终止测试



def discover_and_load_meta_files():
    """
    递归扫描测试目录，加载所有 baseline仓库下metadata.json 文件。
    """
    baseline_data = {}
    tests_root = Path(baseline_local())
    
    # 递归遍历所有子目录
    for meta_file in tests_root.rglob("metadata.json"):
        scenario_dir = meta_file.parent
        relative_path = scenario_dir.relative_to(tests_root)
        module_name = str(relative_path).replace(os.sep, ".")
        
        with open(meta_file, "r", encoding="utf-8") as f:
            data = json.load(f)

        baseline_data[module_name] = data
    return baseline_data

def pytest_configure(config):
    update_baseline() # 更新基准数据
    config.baseline_data = discover_and_load_meta_files() # 加载baseline元数据
