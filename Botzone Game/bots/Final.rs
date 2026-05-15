use std::cmp::max;
use std::f64::consts::E;
use std::io::{self, Read};
use std::time::{Instant, SystemTime, UNIX_EPOCH};

// ==========================================
// 【全局常量与状态定义】
// ==========================================
const SIZE: usize = 15; // 棋盘尺寸（15x15）
const TIME_LIMIT: f64 = 0.90; // 卡时阈值：Botzone平台严格限制1秒，保守设定为0.90秒

// ==========================================
// 【简易的随机数生成器】
// 避免依赖外部 rand crate，以便在评测平台一次性通过编译
// ==========================================
struct XorShiftRng {
    state: u64,
}

impl XorShiftRng {
    fn new() -> Self {
        let nanos = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        XorShiftRng {
            state: (nanos as u64) | 1, // 确保初始状态不为0
        }
    }

    fn next_u32(&mut self) -> u32 {
        let mut x = self.state;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        self.state = x;
        x as u32
    }

    fn gen_range(&mut self, bound: usize) -> usize {
        (self.next_u32() as usize) % bound
    }
}

// ==========================================
// 【基础打分与评估系统 (复用贪心阶段的专家知识)】
// ==========================================

/// 根据连子数量和开口数计算单方向得分
fn get_score(count: i32, open_ends: i32) -> i32 {
    if count >= 5 {
        return 100000; // 连五（必胜）
    }
    if count == 4 {
        return if open_ends == 2 { 10000 } else { 1000 }; // 活四 > 冲四
    }
    if count == 3 {
        return if open_ends == 2 { 1000 } else { 100 }; // 活三 > 冲三
    }
    if count == 2 {
        return if open_ends == 2 { 100 } else { 10 }; // 活二 > 冲二
    }
    0
}

/// 统计单个点位在某一方向上的得分
fn count_direction_score(
    x: i32,
    y: i32,
    dx: i32,
    dy: i32,
    color: i32,
    board: &[[i32; SIZE]; SIZE],
) -> i32 {
    let mut count = 1;
    let mut open_ends = 0;

    // 正向遍历
    let mut i = x + dx;
    let mut j = y + dy;
    while i >= 0 && i < SIZE as i32 && j >= 0 && j < SIZE as i32 && board[i as usize][j as usize] == color {
        count += 1;
        i += dx;
        j += dy;
    }
    if i >= 0 && i < SIZE as i32 && j >= 0 && j < SIZE as i32 && board[i as usize][j as usize] == 0 {
        open_ends += 1;
    }

    // 反向遍历
    let mut i = x - dx;
    let mut j = y - dy;
    while i >= 0 && i < SIZE as i32 && j >= 0 && j < SIZE as i32 && board[i as usize][j as usize] == color {
        count += 1;
        i -= dx;
        j -= dy;
    }
    if i >= 0 && i < SIZE as i32 && j >= 0 && j < SIZE as i32 && board[i as usize][j as usize] == 0 {
        open_ends += 1;
    }

    get_score(count, open_ends)
}

/// 评估单个点位对特定颜色的总价值
fn evaluate_point(x: i32, y: i32, color: i32, board: &[[i32; SIZE]; SIZE]) -> i32 {
    let mut score = 0;
    let dx = [1, 0, 1, 1];
    let dy = [0, 1, 1, -1];
    for k in 0..4 {
        score += count_direction_score(x, y, dx[k], dy[k], color, board);
    }
    score
}

/// 评估单个点位的综合价值（攻防一体）
fn evaluate_point_total(x: i32, y: i32, board: &[[i32; SIZE]; SIZE]) -> i32 {
    evaluate_point(x, y, 1, board) + evaluate_point(x, y, -1, board)
}

/// 评估全局棋盘总分数（我方得分 - 对方得分）
fn evaluate_board(my_color: i32, board: &[[i32; SIZE]; SIZE]) -> i32 {
    let mut my_score = 0;
    let mut opp_score = 0;
    for i in 0..SIZE {
        for j in 0..SIZE {
            if board[i][j] == my_color {
                my_score += evaluate_point(i as i32, j as i32, my_color, board);
            } else if board[i][j] != 0 {
                opp_score += evaluate_point(i as i32, j as i32, -my_color, board);
            }
        }
    }
    my_score - opp_score
}

/// 检查点位是否有邻居棋子（剪枝优化）
fn has_neighbor(x: i32, y: i32, radius: i32, board: &[[i32; SIZE]; SIZE]) -> bool {
    let min_x = max(0, x - radius);
    let max_x = std::cmp::min(SIZE as i32 - 1, x + radius);
    let min_y = max(0, y - radius);
    let max_y = std::cmp::min(SIZE as i32 - 1, y + radius);

    for i in min_x..=max_x {
        for j in min_y..=max_y {
            if i == x && j == y {
                continue;
            }
            if board[i as usize][j as usize] != 0 {
                return true;
            }
        }
    }
    false
}

/// 快速检查某落子是否连五获胜
fn check_win_fast(x: i32, y: i32, color: i32, board: &[[i32; SIZE]; SIZE]) -> bool {
    let dx = [1, 0, 1, 1];
    let dy = [0, 1, 1, -1];
    for k in 0..4 {
        let mut count = 1;
        let mut i = x + dx[k];
        let mut j = y + dy[k];
        while i >= 0 && i < SIZE as i32 && j >= 0 && j < SIZE as i32 && board[i as usize][j as usize] == color {
            count += 1;
            i += dx[k];
            j += dy[k];
        }
        i = x - dx[k];
        j = y - dy[k];
        while i >= 0 && i < SIZE as i32 && j >= 0 && j < SIZE as i32 && board[i as usize][j as usize] == color {
            count += 1;
            i -= dx[k];
            j -= dy[k];
        }
        if count >= 5 {
            return true;
        }
    }
    false
}

// ==========================================
// 【紧急落子拦截网】
// ==========================================
fn get_urgent_move(board: &[[i32; SIZE]; SIZE]) -> Option<(i32, i32)> {
    let mut best_defend = None;
    let mut max_defend_score = -1;

    for i in 0..SIZE {
        for j in 0..SIZE {
            if board[i][j] == 0 && has_neighbor(i as i32, j as i32, 2, board) {
                // 1. 必杀检查：我方落子直接赢
                if evaluate_point(i as i32, j as i32, 1, board) >= 10000 {
                    return Some((i as i32, j as i32));
                }
                // 2. 必防检查：对方落子直接赢
                let opp_score = evaluate_point(i as i32, j as i32, -1, board);
                if opp_score >= 10000 {
                    if opp_score > max_defend_score {
                        max_defend_score = opp_score;
                        best_defend = Some((i as i32, j as i32));
                    }
                }
            }
        }
    }
    best_defend
}

// ==========================================
// 【MCTS核心组件：动作生成与节点结构】
// ==========================================

struct MoveInfo {
    x: i32,
    y: i32,
    score: i32,
}

fn get_heuristic_legal_moves(board: &[[i32; SIZE]; SIZE]) -> Vec<MoveInfo> {
    let mut candidate_moves = Vec::new();
    for i in 0..SIZE {
        for j in 0..SIZE {
            if board[i][j] == 0 && has_neighbor(i as i32, j as i32, 1, board) {
                let score = max(1, evaluate_point_total(i as i32, j as i32, board));
                candidate_moves.push(MoveInfo { x: i as i32, y: j as i32, score });
            }
        }
    }
    // 降序排序，分高的优先
    candidate_moves.sort_by(|a, b| b.score.cmp(&a.score));
    
    // Top-15 截断剪枝
    let keep_count = std::cmp::min(candidate_moves.len(), 15);
    candidate_moves.into_iter().take(keep_count).collect()
}

/// MCTS节点结构（Rust中用Arena数组分配器，以usize索引代替指针）
struct Node {
    move_x: i32,
    move_y: i32,
    color: i32,
    visits: i32,
    wins: f64,
    prior_prob: f64,
    parent: Option<usize>,
    children: Vec<usize>,
    is_expanded: bool,
}

// ==========================================
// MCTS核心逻辑（融入 AlphaZero PUCT）
// ==========================================

fn tree_policy(arena: &mut Vec<Node>, mut node_idx: usize, board: &mut [[i32; SIZE]; SIZE]) -> usize {
    while arena[node_idx].is_expanded && !arena[node_idx].children.is_empty() {
        let mut best_child = 0;
        let mut best_puct = -9999999.0;
        let parent_visits = arena[node_idx].visits as f64;

        for &child_idx in &arena[node_idx].children {
            let child = &arena[child_idx];
            let q = if child.visits == 0 { 0.0 } else { child.wins / child.visits as f64 };
            // 探索系数 C_PUCT = 1.5
            let u = 1.5 * child.prior_prob * parent_visits.sqrt() / (1.0 + child.visits as f64);
            let puct = q + u;
            
            if puct > best_puct {
                best_puct = puct;
                best_child = child_idx;
            }
        }
        node_idx = best_child;
        let node = &arena[node_idx];
        board[node.move_x as usize][node.move_y as usize] = node.color;
    }
    node_idx
}

fn expand(arena: &mut Vec<Node>, node_idx: usize, board: &mut [[i32; SIZE]; SIZE]) -> usize {
    if arena[node_idx].is_expanded {
        return node_idx;
    }

    let moves = get_heuristic_legal_moves(board);
    if moves.is_empty() {
        return node_idx; // 平局/满盘
    }

    let score_sum: f64 = moves.iter().map(|m| m.score as f64).sum();
    let next_color = if arena[node_idx].color == 1 { -1 } else { 1 };

    for m in &moves {
        let prob = if score_sum > 0.0 {
            (m.score as f64) / score_sum
        } else {
            1.0 / moves.len() as f64
        };

        let child = Node {
            move_x: m.x,
            move_y: m.y,
            color: next_color,
            visits: 0,
            wins: 0.0,
            prior_prob: prob,
            parent: Some(node_idx),
            children: Vec::new(),
            is_expanded: false,
        };
        let child_idx = arena.len();
        arena.push(child);
        arena[node_idx].children.push(child_idx);
    }
    arena[node_idx].is_expanded = true;

    // 选择概率最高的子节点（首个）进行模拟
    let best_child_idx = arena[node_idx].children[0];
    let best_child = &arena[best_child_idx];
    board[best_child.move_x as usize][best_child.move_y as usize] = best_child.color;
    
    best_child_idx
}

fn simulate(current_color: i32, board: &mut [[i32; SIZE]; SIZE], rng: &mut XorShiftRng) -> f64 {
    let mut played_moves = Vec::new();
    let mut turn = current_color;
    let mut result = -1.0;

    // 截断模拟：向下随机推演 8 步
    for _ in 0..8 {
        let mut legal_moves = Vec::new();
        for i in 0..SIZE {
            for j in 0..SIZE {
                if board[i][j] == 0 && has_neighbor(i as i32, j as i32, 1, board) {
                    legal_moves.push((i as i32, j as i32));
                }
            }
        }
        if legal_moves.is_empty() {
            break;
        }

        let m = legal_moves[rng.gen_range(legal_moves.len())];
        board[m.0 as usize][m.1 as usize] = turn;
        played_moves.push(m);

        if check_win_fast(m.0, m.1, turn, board) {
            result = if turn == 1 { 1.0 } else { 0.0 };
            break;
        }
        turn = if turn == 1 { -1 } else { 1 };
    }

    // 8 步未分胜负 -> 静态打分映射
    if result < 0.0 {
        let final_score = evaluate_board(1, board);
        let k = 0.005;
        // Sigmoid映射
        result = 1.0 / (1.0 + E.powf(-k * final_score as f64));
    }

    // 撤销模拟落子
    for m in played_moves {
        board[m.0 as usize][m.1 as usize] = 0;
    }
    result
}

fn backpropagate(arena: &mut Vec<Node>, mut node_idx: Option<usize>, result: f64) {
    while let Some(idx) = node_idx {
        arena[idx].visits += 1;
        if arena[idx].color == 1 {
            arena[idx].wins += result;
        } else {
            arena[idx].wins += 1.0 - result;
        }
        node_idx = arena[idx].parent;
    }
}

fn get_best_move_mcts(board: &mut [[i32; SIZE]; SIZE]) -> (i32, i32) {
    // 1. 拦截必杀/必防
    if let Some(urgent_move) = get_urgent_move(board) {
        return urgent_move;
    }

    let mut arena = Vec::new();
    // 根节点
    arena.push(Node {
        move_x: -1,
        move_y: -1,
        color: -1,
        visits: 0,
        wins: 0.0,
        prior_prob: 1.0,
        parent: None,
        children: Vec::new(),
        is_expanded: false,
    });

    let start_time = Instant::now();
    let mut rng = XorShiftRng::new();

    // 时间控制引擎
    while start_time.elapsed().as_secs_f64() < TIME_LIMIT {
        let leaf = tree_policy(&mut arena, 0, board);
        let expanded = expand(&mut arena, leaf, board);
        
        let simulate_color = if arena[expanded].color == 1 { -1 } else { 1 };
        let result = simulate(simulate_color, board, &mut rng);
        backpropagate(&mut arena, Some(expanded), result);

        // 撤销选择和扩展带来的棋盘变化
        let mut temp = expanded;
        while temp != 0 { // 0 是 root
            let node = &arena[temp];
            board[node.move_x as usize][node.move_y as usize] = 0;
            temp = node.parent.unwrap();
        }
    }

    // 选取最稳健策略（访问量最高）
    let mut best_child_idx = 0;
    let mut max_visits = -1;
    for &child_idx in &arena[0].children {
        if arena[child_idx].visits > max_visits {
            max_visits = arena[child_idx].visits;
            best_child_idx = child_idx;
        }
    }

    if arena[0].children.is_empty() {
        return ((SIZE / 2) as i32, (SIZE / 2) as i32);
    }
    let best_node = &arena[best_child_idx];
    (best_node.move_x, best_node.move_y)
}

// ==========================================
// 【主函数与交互接口】
// ==========================================
fn main() {
    let mut input = String::new();
    if io::stdin().read_to_string(&mut input).is_err() {
        return;
    }

    let mut tokens = input.split_whitespace().map(|s| s.parse::<i32>().unwrap());
    
    // n 为落子历史次数
    let n = match tokens.next() {
        Some(val) => val,
        None => return,
    };

    let mut board = [[0; SIZE]; SIZE];
    
    for _ in 0..(n - 1) {
        let ox = tokens.next().unwrap();
        let oy = tokens.next().unwrap();
        if ox != -1 { board[ox as usize][oy as usize] = -1; }
        
        let mx = tokens.next().unwrap();
        let my = tokens.next().unwrap();
        if mx != -1 { board[mx as usize][my as usize] = 1; }
    }

    let last_ox = tokens.next().unwrap();
    let last_oy = tokens.next().unwrap();
    if last_ox != -1 { board[last_ox as usize][last_oy as usize] = -1; }

    // 处理一手交换逻辑与开局
    let (best_x, best_y) = if last_ox != -1 && n == 1 {
        // 我方后手第一回合，强制换手
        (-1, -1)
    } else if n == 1 && last_ox == -1 {
        // 我方先手第一子，落中央
        ((SIZE / 2) as i32, (SIZE / 2) as i32)
    } else {
        // 正常 MCTS 策略
        get_best_move_mcts(&mut board)
    };

    println!("{} {}", best_x, best_y);
}