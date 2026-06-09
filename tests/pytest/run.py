import autoit
import os
import run_case
import time

work_dir = r"D:\WORK-REPOS\C3DSlicer\build_Release\src\Release"
creality_process_name = work_dir+r"\SanityPrint.exe"
log_dir = r"C:\Users\111454\AppData\Roaming\Creality\Sanity Print\6.0 Alpha\log"
log_file = "creality.tlog"

config = {
    "log_name" : "creality.tlog",
    "fmt": "[head]:%s %s[body]:%s"
}

def main():
    try:
        try:
            os.remove(log_dir+'\\'+log_file)
            print("remove last time log file")
        except:
            pass
        autoit.run(creality_process_name)
        time.sleep(15)
        # autoit.win_wait_active(creality_process_name, timeout=-1)
        
        def operation():
            autoit.autoit.mouse_move(287, 87, 1)
            autoit.autoit.mouse_click()
        keyword =config["fmt"]%("wifi_icon", "entry", 200)
        run_case.run(log_dir, config["log_name"], keyword, operation)

        # autoit.win_close(creality_process_name)
        
    
    except Exception as e:
        print(f"error: {e}")

if __name__ == "__main__":
    main()
