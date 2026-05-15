import streamlit as st
import pandas as pd
import os
import subprocess
import time
import glob
import platform
import json
from datetime import datetime

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

# ---------------- 初始化工程目录 ----------------
for folder in ['bots', 'bin', 'logs']:
    os.makedirs(folder, exist_ok=True)

st.set_page_config(page_title="五子棋 AI 自动化评测台", layout="wide")
st.title("🏆 算法对决：五子棋 AI 自动化评测台")

available_bots = sorted([os.path.basename(f) for f in glob.glob('bots/*.cpp')])

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

    st.divider()
    st.subheader("📂 查看历史报告")
    hist_log_paths = sorted(glob.glob('logs/report_*.json'), reverse=True)
    hist_options = ["(新建测试)"] + [os.path.basename(p) for p in hist_log_paths]
    selected_hist = st.selectbox("加载历史报告", hist_options)


# ---------------- 编译 ----------------
def compile_bot(cpp_filename, exe_filepath):
    cpp_filepath = os.path.join('bots', cpp_filename)
    compile_cmd = ["g++", "-O3", cpp_filepath, "-o", exe_filepath]
    try:
        result = subprocess.run(compile_cmd, capture_output=True, text=True)
        if result.returncode != 0:
            return False, result.stderr
        return True, "编译成功"
    except Exception as e:
        return False, str(e)


# ---------------- 棋盘可视化辅助函数 ----------------
def find_winning_cells(history_slice, size=15):
    """返回构成五连胜的坐标集合（若有）。"""
    if not history_slice:
        return set()
    board = [[0] * size for _ in range(size)]
    for item in history_slice:
        x, y, c = item[0], item[1], item[2]
        board[x][y] = c
    lx = history_slice[-1][0]
    ly = history_slice[-1][1]
    lc = history_slice[-1][2]
    directions = [(1, 0), (0, 1), (1, 1), (1, -1)]
    for dx, dy in directions:
        cells = [(lx, ly)]
        nx, ny = lx + dx, ly + dy
        while 0 <= nx < size and 0 <= ny < size and board[nx][ny] == lc:
            cells.append((nx, ny)); nx += dx; ny += dy
        nx, ny = lx - dx, ly - dy
        while 0 <= nx < size and 0 <= ny < size and board[nx][ny] == lc:
            cells.append((nx, ny)); nx -= dx; ny -= dy
        if len(cells) >= 5:
            return set(cells)
    return set()


def draw_board(history_slice, size=15, winning_cells=None, show_win=True):
    """绘制棋盘为 matplotlib Figure。history_slice 每项为 [x, y, color]。"""
    fig, ax = plt.subplots(figsize=(7, 7))
    ax.set_facecolor('#DEB887')
    fig.patch.set_facecolor('#DEB887')

    # 网格线
    for i in range(size):
        ax.axhline(i, color='#4a3728', lw=0.8, zorder=1)
        ax.axvline(i, color='#4a3728', lw=0.8, zorder=1)

    # 外框加粗
    for spine in ax.spines.values():
        spine.set_linewidth(2)
        spine.set_color('#4a3728')

    # 星位：天元 (7,7) + 四星 (3,3)(3,11)(11,3)(11,11)
    for sx, sy in [(3, 3), (3, 11), (7, 7), (11, 3), (11, 11)]:
        ax.plot(sy, sx, 'o', color='#4a3728', ms=5, zorder=2)

    # 落子
    for k, item in enumerate(history_slice):
        x, y, color = item[0], item[1], item[2]
        is_black = (color == 1)
        fc = '#1a1a1a' if is_black else '#f5f5f5'
        tc = '#f5f5f5' if is_black else '#1a1a1a'
        is_winner = bool(winning_cells and (x, y) in winning_cells and show_win)
        ec = '#FFD700' if is_winner else ('#555' if is_black else '#aaa')
        lw = 2.8 if is_winner else 1.2
        circle = plt.Circle((y, x), 0.44, fc=fc, ec=ec, lw=lw, zorder=3)
        ax.add_patch(circle)
        ax.text(y, x, str(k + 1), ha='center', va='center',
                color=tc, fontsize=5.5, fontweight='bold', zorder=4)

    # 最后一手红点标记
    if history_slice:
        lx, ly = history_slice[-1][0], history_slice[-1][1]
        ax.plot(ly, lx, 'r.', ms=7, zorder=5)

    ax.set_xlim(-0.5, size - 0.5)
    ax.set_ylim(-0.5, size - 0.5)
    ax.set_aspect('equal')
    ax.invert_yaxis()
    ax.set_xticks(range(size))
    ax.set_yticks(range(size))
    ax.set_xticklabels([str(i) for i in range(size)], fontsize=7)
    ax.set_yticklabels([str(i) for i in range(size)], fontsize=7)
    plt.tight_layout()
    return fig


# ---------------- 三标签页结果展示 ----------------
def render_result_tabs(match_logs, bot_a_name, bot_b_name):
    """渲染评估报告、棋局回放、数据图表三个标签页。"""
    if not match_logs:
        st.warning("暂无对战数据。")
        return

    total_games = len(match_logs)

    # === 计算汇总统计 ===
    all_moves = [m['total_moves'] for m in match_logs]
    avg_moves = sum(all_moves) / total_games
    min_moves = min(all_moves)
    max_moves = max(all_moves)

    bot_a_times, bot_b_times = [], []
    swap_count = 0
    for m in match_logs:
        if m.get('swapped', False):
            swap_count += 1
        times = m.get('move_times_sec', [])
        a_is_black = (m['a_role'] == 'Black')
        for k, t in enumerate(times):
            if (k % 2 == 0) == a_is_black:
                bot_a_times.append(t)
            else:
                bot_b_times.append(t)

    avg_time_a = (sum(bot_a_times) / len(bot_a_times) * 1000) if bot_a_times else None
    avg_time_b = (sum(bot_b_times) / len(bot_b_times) * 1000) if bot_b_times else None
    swap_rate = swap_count / total_games * 100

    black_games = [m for m in match_logs if m['a_role'] == 'Black']
    white_games = [m for m in match_logs if m['a_role'] == 'White']

    def side_stats(games):
        n = len(games)
        w = sum(1 for g in games if g['outcome'] == 'Win')
        l = sum(1 for g in games if g['outcome'] == 'Loss')
        d = sum(1 for g in games if g['outcome'] == 'Draw')
        e = sum(1 for g in games if g['reason'] not in ('Normal',))
        return n, w, l, d, e

    bn, bw, bl, bd, be = side_stats(black_games)
    wn, ww, wl, wd, we = side_stats(white_games)

    def fmt(n, v):
        return f"{v / n * 100:.1f}%" if n else "N/A"

    tab1, tab2, tab3 = st.tabs(["📊 评估报告", "🎮 棋局回放", "📈 数据图表"])

    # ===================== TAB 1：评估报告 =====================
    with tab1:
        st.header(f"📊 权威评估报告：{bot_a_name}")

        report_df = pd.DataFrame({
            "评估阵营":       ["执黑 (先手)", "执白 (后手)"],
            "胜率":           [fmt(bn, bw), fmt(wn, ww)],
            "败率":           [fmt(bn, bl), fmt(wn, wl)],
            "平局率":         [fmt(bn, bd), fmt(wn, wd)],
            "异常崩溃率":     [fmt(bn, be), fmt(wn, we)],
        })
        st.dataframe(report_df, use_container_width=True, hide_index=True)

        st.divider()
        c1, c2, c3, c4, c5 = st.columns(5)
        c1.metric("总对战局数", total_games)
        c2.metric("平均步数", f"{avg_moves:.1f}", f"范围 {min_moves}–{max_moves}")
        c3.metric(
            f"Bot A 均思考/步",
            f"{avg_time_a:.1f} ms" if avg_time_a is not None else "N/A"
        )
        c4.metric(
            f"Bot B 均思考/步",
            f"{avg_time_b:.1f} ms" if avg_time_b is not None else "N/A"
        )
        c5.metric("Swap1 换手率", f"{swap_rate:.1f}%")

        st.divider()
        st.markdown("### 📝 完整对战记录")
        display_cols = ['game_id', 'a_role', 'outcome', 'reason',
                        'total_moves', 'duration_sec', 'swapped']
        display_rows = [{k: m.get(k, '') for k in display_cols} for m in match_logs]
        st.dataframe(pd.DataFrame(display_rows), use_container_width=True, hide_index=True)

    # ===================== TAB 2：棋局回放 =====================
    with tab2:
        st.subheader("🎮 棋局回放")

        game_labels = [
            f"第{m['game_id']}局  [{m['a_role']}] {bot_a_name[:12]}  "
            f"→ {m['outcome']}  ({m['total_moves']}步)"
            for m in match_logs
        ]
        sel_idx = st.selectbox("选择对局", range(len(match_logs)),
                               format_func=lambda i: game_labels[i],
                               key="replay_game_select")

        sel_game = match_logs[sel_idx]
        seq = sel_game.get('moves_sequence', [])

        if not seq:
            st.info("该记录不含棋谱数据（旧格式日志）。")
        else:
            total_m = len(seq)
            step = st.slider("当前步数（拖动回放）",
                             min_value=0, max_value=total_m,
                             value=total_m, step=1, key="replay_step_slider")

            history_slice = seq[:step]

            # 计算五连胜坐标（仅在终局时高亮）
            winning_cells = set()
            if (step == total_m
                    and sel_game.get('reason') == 'Normal'
                    and sel_game.get('outcome') in ('Win', 'Loss')):
                winning_cells = find_winning_cells(history_slice)

            col_board, col_info = st.columns([3, 2])

            with col_board:
                fig = draw_board(history_slice, winning_cells=winning_cells,
                                 show_win=(step == total_m))
                st.pyplot(fig, use_container_width=True)
                plt.close(fig)

            with col_info:
                st.markdown(f"**进度：** {step} / {total_m} 步")
                if step > 0:
                    last = history_slice[-1]
                    stone_color = "⚫ 黑方" if last[2] == 1 else "⚪ 白方"
                    st.markdown(f"**最后落子：** {stone_color} → `({last[0]}, {last[1]})`")
                swapped_str = "✅ 是" if sel_game.get('swapped') else "❌ 否"
                st.markdown(f"**Swap1 换手：** {swapped_str}")
                outcome_map = {'Win': '✅ 胜', 'Loss': '❌ 负', 'Draw': '🤝 平'}
                st.markdown(f"**Bot A 结果：** {outcome_map.get(sel_game['outcome'], sel_game['outcome'])}")

                st.divider()
                st.markdown("**最近 20 步落子记录**")
                times_list = sel_game.get('move_times_sec', [])
                move_rows = []
                for k in range(min(step, len(seq))):
                    item = seq[k]
                    x, y, c = item[0], item[1], item[2]
                    t = times_list[k] * 1000 if k < len(times_list) else None
                    move_rows.append({
                        "步": k + 1,
                        "颜色": "黑" if c == 1 else "白",
                        "坐标": f"({x}, {y})",
                        "耗时(ms)": f"{t:.0f}" if t is not None else "—",
                    })
                if move_rows:
                    st.dataframe(pd.DataFrame(move_rows[-20:]),
                                 use_container_width=True, hide_index=True)

    # ===================== TAB 3：数据图表 =====================
    with tab3:
        st.subheader("📈 数据图表")

        # --- 胜率趋势（滚动均值）---
        st.markdown("#### 胜率趋势（Bot A，滚动窗口=5局）")
        outcomes_seq = [1 if m['outcome'] == 'Win' else 0 for m in match_logs]
        window = 5
        rolling = []
        for i in range(len(outcomes_seq)):
            s = max(0, i - window + 1)
            rolling.append(round(sum(outcomes_seq[s:i + 1]) / (i - s + 1), 3))
        df_trend = pd.DataFrame(
            {'Bot A 胜率': rolling},
            index=[m['game_id'] for m in match_logs]
        )
        st.line_chart(df_trend, height=250)

        st.divider()

        col_hist, col_timing = st.columns(2)

        # --- 步数分布直方图 ---
        with col_hist:
            st.markdown("#### 步数分布（每10步一区间）")
            bucket_size = 10
            hist = {}
            for mv in all_moves:
                b = (mv // bucket_size) * bucket_size
                k = f"{b}–{b + bucket_size - 1}"
                hist[k] = hist.get(k, 0) + 1
            sorted_hist = dict(
                sorted(hist.items(), key=lambda x: int(x[0].split('–')[0]))
            )
            df_hist = pd.DataFrame(
                {'局数': list(sorted_hist.values())},
                index=list(sorted_hist.keys())
            )
            st.bar_chart(df_hist, height=250)

        # --- 每步思考时间对比 ---
        with col_timing:
            st.markdown("#### 平均每步思考时间对比")
            if avg_time_a is not None or avg_time_b is not None:
                labels, values = [], []
                if avg_time_a is not None:
                    labels.append(bot_a_name[:18])
                    values.append(round(avg_time_a, 2))
                if avg_time_b is not None:
                    labels.append(bot_b_name[:18])
                    values.append(round(avg_time_b, 2))
                df_timing = pd.DataFrame(
                    {'平均思考时间 (ms/步)': values},
                    index=labels
                )
                st.bar_chart(df_timing, height=250)
            else:
                st.info("无每步耗时数据（旧格式日志）。")

        # --- 各局耗时散点 ---
        st.divider()
        st.markdown("#### 各局总耗时（秒）")
        df_dur = pd.DataFrame(
            {'耗时(秒)': [m['duration_sec'] for m in match_logs]},
            index=[m['game_id'] for m in match_logs]
        )
        st.line_chart(df_dur, height=200)


# ---------------- 核心裁判引擎 ----------------
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
            if count >= 5:
                return True
        return False

    def generate_input_for_bot(self, history, is_black_turn):
        """
        生成与 cin >> n 协议匹配的输入字符串。
        history 中每项为 (x, y, color)，通过 [0][1] 索引访问坐标。
        """
        n = len(history) // 2 + 1
        lines = [str(n)]

        if is_black_turn:
            for i in range(n - 1):
                if i == 0:
                    lines.append("-1 -1")
                else:
                    lines.append(f"{history[i*2 - 1][0]} {history[i*2 - 1][1]}")
                lines.append(f"{history[i*2][0]} {history[i*2][1]}")
            if n == 1:
                lines.append("-1 -1")
            else:
                lines.append(f"{history[-1][0]} {history[-1][1]}")
        else:
            for i in range(n - 1):
                lines.append(f"{history[i*2][0]} {history[i*2][1]}")
                lines.append(f"{history[i*2 + 1][0]} {history[i*2 + 1][1]}")
            lines.append(f"{history[-1][0]} {history[-1][1]}")

        return "\n".join(lines) + "\n"

    def play_match(self, exe_black, exe_white, time_limit):
        """
        运行一局对弈。
        返回: (winner, reason, moves_count, duration, history, move_times, was_swapped)
          history: list of (x, y, color)，color=1 黑 / -1 白（换手后颜色已修正）
          move_times: list of float，每颗棋子的思考耗时（秒）
        """
        board = [[0] * self.size for _ in range(self.size)]
        history = []      # (x, y, color)
        move_times = []   # 每步耗时
        moves_count = 0
        start_time = time.time()
        self.error_dump = None
        was_swapped = False

        perspective_flipped = False
        original_exe_black = exe_black
        original_exe_white = exe_white

        def determine_winner(is_current_black_winner):
            winning_exe = exe_black if is_current_black_winner else exe_white
            return "Black_Win" if winning_exe == original_exe_black else "White_Win"

        while moves_count < self.size * self.size:
            is_black_turn = (moves_count % 2 == 0)
            current_exe = exe_black if is_black_turn else exe_white
            current_color = 1 if is_black_turn else -1

            # --- 生成输入（换手后视角 XOR 翻转）---
            input_as_black = is_black_turn ^ perspective_flipped

            # 换手后新黑方首次落子：补充换手作为第1轮，防止再次触发换手条件
            if perspective_flipped and len(history) == 1 and not input_as_black:
                bx, by = history[0][0], history[0][1]
                input_str = f"2\n{bx} {by}\n-1 -1\n-1 -1\n"
            else:
                input_str = self.generate_input_for_bot(history, input_as_black)

            # --- 运行子进程 ---
            move_start = time.time()
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
                return (determine_winner(not is_black_turn), "Timeout",
                        moves_count, time.time() - start_time,
                        history, move_times, was_swapped)
            move_elapsed = time.time() - move_start

            if proc.returncode != 0:
                self.error_dump = (f"【运行时崩溃】Exit Code: {proc.returncode}\n"
                                   f"Stderr: {proc.stderr}")
                return (determine_winner(not is_black_turn), "Crash",
                        moves_count, time.time() - start_time,
                        history, move_times, was_swapped)

            # --- 解析输出 ---
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
                    return (determine_winner(not is_black_turn), "Invalid_Output",
                            moves_count, time.time() - start_time,
                            history, move_times, was_swapped)
            else:
                self.error_dump = (
                    f"【没有找到坐标】\n"
                    f"喂给程序的输入:\n{input_str}\n程序的输出:\n{raw_output}"
                )
                return (determine_winner(not is_black_turn), "Invalid_Output",
                        moves_count, time.time() - start_time,
                        history, move_times, was_swapped)

            # --- Swap1 换手处理 ---
            if not is_black_turn and moves_count == 1 and move_x == -1 and move_y == -1:
                exe_black, exe_white = exe_white, exe_black
                perspective_flipped = True
                was_swapped = True
                if history:
                    bx, by = history[0][0], history[0][1]
                    board[bx][by] = -1
                    history[0] = (bx, by, -1)  # 修正历史中该棋子的颜色
                moves_count += 1
                continue

            # --- 校验落子合法性 ---
            if not (0 <= move_x < self.size and 0 <= move_y < self.size):
                self.error_dump = f"【坐标越界】: ({move_x}, {move_y})"
                return (determine_winner(not is_black_turn), "Invalid_Move",
                        moves_count, time.time() - start_time,
                        history, move_times, was_swapped)
            if board[move_x][move_y] != 0:
                self.error_dump = f"【覆盖落子】: 试图下在已有棋子的 ({move_x}, {move_y})"
                return (determine_winner(not is_black_turn), "Invalid_Move",
                        moves_count, time.time() - start_time,
                        history, move_times, was_swapped)

            # --- 更新状态 ---
            board[move_x][move_y] = current_color
            history.append((move_x, move_y, current_color))
            move_times.append(round(move_elapsed, 4))

            if self.check_win(board, move_x, move_y, current_color):
                return (determine_winner(is_black_turn), "Normal",
                        moves_count + 1, time.time() - start_time,
                        history, move_times, was_swapped)

            moves_count += 1

        return ("Draw", "Normal", moves_count, time.time() - start_time,
                history, move_times, was_swapped)


# ---------------- UI 主流程 ----------------
if start_btn:
    st.info("🔨 正在编译代码...")
    exe_suffix = ".exe" if platform.system() == "Windows" else ""
    exe_a = os.path.join('bin', bot_a_name.replace(".cpp", exe_suffix))
    exe_b = os.path.join('bin', bot_b_name.replace(".cpp", exe_suffix))

    ok_a, msg_a = compile_bot(bot_a_name, exe_a)
    ok_b, msg_b = compile_bot(bot_b_name, exe_b)
    if not ok_a:
        st.error(f"❌ {bot_a_name} 编译失败！\n{msg_a}"); st.stop()
    if not ok_b:
        st.error(f"❌ {bot_b_name} 编译失败！\n{msg_b}"); st.stop()

    st.success("✅ 编译成功！对战即将开始...")

    total_games = int(single_side_games) * 2
    progress_bar = st.progress(0)
    status_text = st.empty()
    error_display = st.empty()

    match_logs = []
    referee = SimpleInteractionReferee()

    for i in range(total_games):
        game_id = i + 1
        a_is_black = i < single_side_games
        current_role = "Black" if a_is_black else "White"

        exe_black = exe_a if a_is_black else exe_b
        exe_white = exe_b if a_is_black else exe_a

        winner, reason, moves, duration, history, move_times, was_swapped = \
            referee.play_match(exe_black, exe_white, time_limit)

        if winner == "Draw":
            outcome = "Draw"
        elif winner == "Black_Win":
            outcome = "Win" if a_is_black else "Loss"
        else:
            outcome = "Win" if not a_is_black else "Loss"

        if reason != "Normal" and referee.error_dump:
            error_display.error(
                f"⚠️ **第 {game_id} 局触发异常 [{reason}]！**\n\n"
                f"**现场回溯:**\n```text\n{referee.error_dump}\n```"
            )

        match_logs.append({
            "game_id":        game_id,
            "a_role":         current_role,
            "outcome":        outcome,
            "reason":         reason,
            "total_moves":    moves,
            "duration_sec":   round(duration, 3),
            "swapped":        was_swapped,
            "moves_sequence": [[x, y, c] for x, y, c in history],
            "move_times_sec": move_times,
        })

        progress_bar.progress(game_id / total_games)
        status_text.text(
            f"⚔️ 战况推进: {game_id}/{total_games} | "
            f"步数: {moves} | 耗时: {round(duration, 2)}秒"
        )

    # 保存结构化日志
    st.divider()
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    log_filepath = os.path.join('logs', f"report_{timestamp}.json")
    log_data = {
        "experiment_meta": {
            "timestamp":         timestamp,
            "bot_a":             bot_a_name,
            "bot_b":             bot_b_name,
            "single_side_games": int(single_side_games),
            "time_limit_sec":    time_limit,
        },
        "matches": match_logs,
    }
    with open(log_filepath, 'w', encoding='utf-8') as f:
        json.dump(log_data, f, indent=2, ensure_ascii=False)
    st.success(f"💾 完整对战记录（含棋谱）已写入：`{log_filepath}`")

    render_result_tabs(match_logs, bot_a_name, bot_b_name)

elif selected_hist != "(新建测试)":
    # 加载历史报告
    log_path = next(
        (p for p in hist_log_paths if os.path.basename(p) == selected_hist), None
    )
    if log_path is None:
        st.error("找不到该报告文件。")
    else:
        try:
            with open(log_path, encoding='utf-8') as f:
                data = json.load(f)
            if isinstance(data, list):
                match_logs = data
                meta = {}
            else:
                match_logs = data.get('matches', [])
                meta = data.get('experiment_meta', {})

            bot_a = meta.get('bot_a', '未知 Bot A')
            bot_b = meta.get('bot_b', '未知 Bot B')

            st.subheader(f"📂 历史报告：{selected_hist}")
            if meta:
                st.caption(
                    f"时间：{meta.get('timestamp', '?')}  |  "
                    f"Bot A：{bot_a}  |  Bot B：{bot_b}  |  "
                    f"单边局数：{meta.get('single_side_games', '?')}  |  "
                    f"限时：{meta.get('time_limit_sec', '?')}s"
                )
            render_result_tabs(match_logs, bot_a, bot_b)
        except Exception as e:
            st.error(f"加载报告失败：{e}")

else:
    st.info("👈 请在左侧配置赛事并点击「编译并开始测试」，或在下方选择历史报告查看。")
