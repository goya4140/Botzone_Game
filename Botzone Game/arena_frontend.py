import streamlit as st
import pandas as pd
import os
import subprocess
import time
import glob
import platform
import json
from datetime import datetime

# ---------------- 初始化工程目录 ----------------
for folder in ['bots', 'bin', 'logs']:
    os.makedirs(folder, exist_ok=True)

st.set_page_config(page_title="五子棋 AI 自动化评测台", layout="wide")
st.title("🏆 算法对决：五子棋 AI 自动化评测台 (纯净输入版)")

available_bots = [os.path.basename(f) for f in glob.glob('bots/*.cpp')]

with st.sidebar:
    st.header("⚙️ 赛事配置")
    if not available_bots:
        st.error("⚠️ 在 `bots/` 目录下没有找到任何 `.cpp` 文件！")
        st.stop()

    bot_a_name = st.selectbox("Bot A (测试目标)", available_bots, index=0)
    default_b_index = 1 if len(available_bots) > 1 else 0
    bot_b_name = st.selectbox("Bot B (基准对手)", available_bots, index=default_b_index)

    st.divider()
    single_side_games = st.number_input("单边对战局数", min_value=1, max_value=5000, value=10, step=10)
    time_limit = st.slider("单步限时 (秒)", 0.5, 5.0, 1.0)
    start_btn = st.button("🚀 编译并开始测试")

def compile_bot(cpp_filename, exe_filepath):
    cpp_filepath = os.path.join('bots', cpp_filename)
    compile_cmd = ["g++", "-O3", cpp_filepath, "-o", exe_filepath]
    try:
        result = subprocess.run(compile_cmd, capture_output=True, text=True)
        if result.returncode != 0: return False, result.stderr
        return True, "编译成功"
    except Exception as e:
        return False, str(e)

# ---------------- 核心裁判引擎：严格匹配 cin >> n 协议 ----------------
class SimpleInteractionReferee:
    def __init__(self, size=15):
        self.size = size
        self.error_dump = None

    def check_win(self, board, x, y, color):
        directions = [(1, 0), (0, 1), (1, 1), (1, -1)]
        for dx, dy in directions:
            count = 1
            nx, ny = x + dx, y + dy
            while 0 <= nx < self.size and 0 <= ny < self.size and board[nx][ny] == color:
                count += 1; nx += dx; ny += dy
            nx, ny = x - dx, y - dy
            while 0 <= nx < self.size and 0 <= ny < self.size and board[nx][ny] == color:
                count += 1; nx -= dx; ny -= dy
            if count >= 5: return True
        return False

    def generate_input_for_bot(self, history, is_black_turn):
        """
        精准生成与 Gomoku_simple.cpp 中 cin >> n 匹配的多行字符串。
        
        is_black_turn 表示"从哪方视角生成输入"：
          True  → 黑方视角（自己先手，对手第一轮无落子）
          False → 白方视角（对手先手，第一轮就有一颗对手棋子）
        
        注意：换手后，该参数会被翻转（perspective_flipped），
        与 moves_count 决定的"谁来行棋"解耦。
        """
        n = len(history) // 2 + 1
        lines = [str(n)]  # 第一行：总步数 n

        if is_black_turn:
            # 黑方视角：第1轮对手无落子（-1 -1），之后交替读取对手/自己
            for i in range(n - 1):
                if i == 0:
                    lines.append("-1 -1")  # 黑方先手，第1轮对手无子
                else:
                    lines.append(f"{history[i*2 - 1][0]} {history[i*2 - 1][1]}")
                lines.append(f"{history[i*2][0]} {history[i*2][1]}")
            # 读取对手最后一步
            if n == 1:
                lines.append("-1 -1")
            else:
                lines.append(f"{history[-1][0]} {history[-1][1]}")
        else:
            # 白方视角：对手先手，第1轮就已有一颗对手棋子
            for i in range(n - 1):
                lines.append(f"{history[i*2][0]} {history[i*2][1]}")
                lines.append(f"{history[i*2 + 1][0]} {history[i*2 + 1][1]}")
            # 读取对手最后一步
            lines.append(f"{history[-1][0]} {history[-1][1]}")

        return "\n".join(lines) + "\n"

    def play_match(self, exe_black, exe_white, time_limit):
        board = [[0] * self.size for _ in range(self.size)]
        history = []  # 按落子顺序记录所有坐标 (x, y)

        moves_count = 0
        start_time = time.time()
        self.error_dump = None

        # ============================================================
        # ★ Swap1换手支持 ★
        #
        # 问题根源：白方第一步合法输出 -1 -1 表示换手，
        # 旧代码直接判坐标越界→判黑方胜，导致所有对局第1步即结束。
        #
        # 修复方案：
        #   1. perspective_flipped：换手后，双方收到的输入视角互换。
        #      原黑方改收"黑方视角"输入，原白方改收"白方视角"输入。
        #      实现方式：is_black_turn XOR perspective_flipped。
        #
        #   2. 棋盘颜色修正：换手后，原黑方第一颗棋子（颜色=1）
        #      归属变为新白方，需改为 -1。
        #
        #   3. 胜负归属修正：换手后exe_black/exe_white已互换，
        #      必须对照 original_exe_black 判断谁赢了。
        # ============================================================
        perspective_flipped = False       # 换手后置为 True
        original_exe_black = exe_black    # 记录原始黑方，胜负判定用
        original_exe_white = exe_white    # 记录原始白方，胜负判定用

        def determine_winner(is_current_black_winner):
            """
            根据原始执方分配，返回正确的胜者标识。
            换手后 exe_black 已变为原白方，不能再直接用 is_black_turn 判断。
            """
            winning_exe = exe_black if is_current_black_winner else exe_white
            return "Black_Win" if winning_exe == original_exe_black else "White_Win"

        while moves_count < self.size * self.size:
            is_black_turn = (moves_count % 2 == 0)
            current_exe = exe_black if is_black_turn else exe_white
            current_color = 1 if is_black_turn else -1

            # --- 1. 生成输入：换手后视角翻转（XOR） ---
            input_as_black = is_black_turn ^ perspective_flipped

            # ★ 修复：换手后新黑方首次落子时，原来的 generate_input_for_bot 会
            # 生成 "1\n{bx} {by}\n"（n=1 且对手有效坐标），与白方"是否换手"的
            # 输入完全相同，导致程序再次输出 -1 -1 → Invalid_Move。
            # 解决方案：补充换手作为第1轮历史，改为 n=2 格式，使 n==1 条件不成立。
            if perspective_flipped and len(history) == 1 and not input_as_black:
                bx, by = history[0]
                input_str = f"2\n{bx} {by}\n-1 -1\n-1 -1\n"
            else:
                input_str = self.generate_input_for_bot(history, input_as_black)

            # --- 2. 启动子进程 ---
            try:
                proc = subprocess.run(
                    [current_exe],
                    input=input_str,
                    text=True,
                    capture_output=True,
                    timeout=time_limit
                )
            except subprocess.TimeoutExpired:
                self.error_dump = f"【超时被杀】程序运算超出了限时 {time_limit} 秒。"
                return determine_winner(not is_black_turn), "Timeout", moves_count, time.time() - start_time

            if proc.returncode != 0:
                self.error_dump = f"【运行时崩溃】Exit Code: {proc.returncode}\nStderr: {proc.stderr}"
                return determine_winner(not is_black_turn), "Crash", moves_count, time.time() - start_time

            # --- 3. 解析输出 (抓取最后两个数字) ---
            raw_output = proc.stdout.strip()
            tokens = raw_output.split()
            if len(tokens) >= 2:
                try:
                    move_x = int(tokens[-2])
                    move_y = int(tokens[-1])
                except ValueError:
                    self.error_dump = (
                        f"【输出格式错误】无法解析坐标。\n"
                        f"喂给程序的输入:\n{input_str}\n程序的输出:\n{raw_output}"
                    )
                    return determine_winner(not is_black_turn), "Invalid_Output", moves_count, time.time() - start_time
            else:
                self.error_dump = (
                    f"【没有找到坐标】\n"
                    f"喂给程序的输入:\n{input_str}\n程序的输出:\n{raw_output}"
                )
                return determine_winner(not is_black_turn), "Invalid_Output", moves_count, time.time() - start_time

            # --- 4. ★ 关键修复：优先处理合法换手 ★ ---
            # 条件：后手 & 第1步（moves_count==1）& 输出 -1 -1
            # 旧代码在此处直接判越界，是导致批量测试失效的根本原因！
            if not is_black_turn and moves_count == 1 and move_x == -1 and move_y == -1:
                # 合法换手！三步操作：
                # ① 交换可执行文件（原白方变新黑方，原黑方变新白方）
                exe_black, exe_white = exe_white, exe_black
                # ② 翻转视角标志（此后输入生成用相反视角）
                perspective_flipped = True
                # ③ 修正棋盘颜色：原黑方第一颗棋子归属变为新白方（颜色1→-1）
                if history:
                    bx, by = history[0]
                    board[bx][by] = -1
                moves_count += 1
                continue  # 继续循环，由新黑方（原白方）落子

            # --- 5. 校验普通落子 ---
            if not (0 <= move_x < self.size and 0 <= move_y < self.size):
                self.error_dump = f"【坐标越界】: ({move_x}, {move_y})"
                return determine_winner(not is_black_turn), "Invalid_Move", moves_count, time.time() - start_time
            if board[move_x][move_y] != 0:
                self.error_dump = f"【覆盖落子】: 试图下在已有棋子的 ({move_x}, {move_y})"
                return determine_winner(not is_black_turn), "Invalid_Move", moves_count, time.time() - start_time

            # --- 6. 更新状态 ---
            board[move_x][move_y] = current_color
            history.append((move_x, move_y))

            # 判赢
            if self.check_win(board, move_x, move_y, current_color):
                return determine_winner(is_black_turn), "Normal", moves_count + 1, time.time() - start_time

            moves_count += 1

        return "Draw", "Normal", moves_count, time.time() - start_time


# ---------------- UI 交互与调度 ----------------
if start_btn:
    st.info("🔨 正在编译代码...")
    exe_suffix = ".exe" if platform.system() == "Windows" else ""
    exe_a = os.path.join('bin', bot_a_name.replace(".cpp", exe_suffix))
    exe_b = os.path.join('bin', bot_b_name.replace(".cpp", exe_suffix))

    if not compile_bot(bot_a_name, exe_a)[0]: st.error(f"❌ {bot_a_name} 编译失败！"); st.stop()
    if not compile_bot(bot_b_name, exe_b)[0]: st.error(f"❌ {bot_b_name} 编译失败！"); st.stop()

    st.success("✅ 编译成功！对战即将开始...")

    total_games = single_side_games * 2
    progress_bar = st.progress(0)
    status_text = st.empty()
    error_display = st.empty()

    stats = {"Black": {"Win": 0, "Loss": 0, "Draw": 0, "Error": 0}, "White": {"Win": 0, "Loss": 0, "Draw": 0, "Error": 0}}
    match_logs = []

    referee = SimpleInteractionReferee()

    for i in range(total_games):
        game_id = i + 1
        a_is_black = i < single_side_games
        current_role = "Black" if a_is_black else "White"

        exe_black = exe_a if a_is_black else exe_b
        exe_white = exe_b if a_is_black else exe_a

        winner, reason, moves, duration = referee.play_match(exe_black, exe_white, time_limit)

        if winner == "Draw":
            outcome = "Draw"
        elif winner == "Black_Win":
            outcome = "Win" if a_is_black else "Loss"
        else:
            outcome = "Win" if not a_is_black else "Loss"

        if reason != "Normal":
            if winner == "Black_Win" and not a_is_black: stats[current_role]["Error"] += 1
            elif winner == "White_Win" and a_is_black: stats[current_role]["Error"] += 1

            if referee.error_dump:
                error_display.error(
                    f"⚠️ **第 {game_id} 局触发异常 [{reason}]！**\n\n"
                    f"**现场回溯:**\n```text\n{referee.error_dump}\n```"
                )

        stats[current_role][outcome] += 1
        match_logs.append({
            "game_id": game_id, "a_role": current_role, "outcome": outcome,
            "reason": reason, "total_moves": moves, "duration_sec": round(duration, 3)
        })

        progress_bar.progress(game_id / total_games)
        status_text.text(f"⚔️ 战况推进: {game_id}/{total_games} | 步数: {moves} | 耗时: {round(duration, 2)}秒")

    st.divider()
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    log_filepath = os.path.join('logs', f"report_{timestamp}.json")
    with open(log_filepath, 'w', encoding='utf-8') as f:
        json.dump(match_logs, f, indent=4, ensure_ascii=False)

    st.header(f"📊 权威评估报告：{bot_a_name}")
    st.success(f"💾 对战序列及异常现场已写入本地存储：`{log_filepath}`")

    def calc_rates(d, total):
        if total == 0: return 0, 0, 0, 0
        return d["Win"]/total*100, d["Loss"]/total*100, d["Draw"]/total*100, d["Error"]/total*100

    b_w, b_l, b_d, b_e = calc_rates(stats["Black"], single_side_games)
    w_w, w_l, w_d, w_e = calc_rates(stats["White"], single_side_games)

    report_df = pd.DataFrame({
        "评估阵营": ["执黑 (先手)", "执白 (后手)"],
        "胜率": [f"{b_w:.1f}%", f"{w_w:.1f}%"],
        "败率": [f"{b_l:.1f}%", f"{w_l:.1f}%"],
        "平局率": [f"{b_d:.1f}%", f"{w_d:.1f}%"],
        "异常崩溃率 (需严控)": [f"{b_e:.1f}%", f"{w_e:.1f}%"]
    })
    st.dataframe(report_df, use_container_width=True, hide_index=True)
    st.markdown("### 📝 战况回放简报 (节选)")
    st.dataframe(pd.DataFrame(match_logs[:15]), use_container_width=True, hide_index=True)