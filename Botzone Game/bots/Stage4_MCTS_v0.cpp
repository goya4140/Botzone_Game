#include <iostream>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <ctime>

using namespace std;

// 棋盘尺寸：15x15 标准五子棋棋盘
const int SIZE = 15;
// 卡时阈值：文档建议0.95秒，保守设为0.9秒（转换为clock()的计数值），避免超时无输出
// CLOCKS_PER_SEC是每秒的时钟滴答数，用于将秒转换为clock()的单位
const double TIME_LIMIT = 0.90 * CLOCKS_PER_SEC; 

// 全局棋盘状态：0=空，1=我方落子，-1=对方落子
int board[SIZE][SIZE] = { 0 };

// --- 基础工具函数 ---
/**
 * @brief 检查指定坐标周围radius范围内是否有落子（优化合法落子点搜索）
 * @param x 横坐标
 * @param y 纵坐标
 * @param radius 搜索半径（默认1，即3x3范围）
 * @return 有相邻落子返回true，否则false
 */
bool hasNeighbor(int x, int y, int radius = 1) {
    // 遍历radius范围内的所有坐标（防止越界）
    for (int i = max(0, x - radius); i <= min(SIZE - 1, x + radius); i++) {
        for (int j = max(0, y - radius); j <= min(SIZE - 1, y + radius); j++) {
            if (i == x && j == y) continue; // 跳过自身坐标
            if (board[i][j] != 0) return true; // 发现非空落子，直接返回
        }
    }
    return false;
}

/**
 * @brief 快速检查指定坐标落子后是否形成五子连珠（胜负判定）
 * @param x 落子横坐标
 * @param y 落子纵坐标
 * @param color 落子方颜色（1/-1）
 * @return 形成五子连珠返回true，否则false
 */
bool checkWinFast(int x, int y, int color) {
    // 四个检查方向：横、竖、正斜、反斜
    int dx[4] = {1, 0, 1, 1};
    int dy[4] = {0, 1, 1, -1};
    
    // 遍历四个方向
    for (int k = 0; k < 4; k++) {
        int count = 1; // 当前方向连续同色棋子数（初始为当前落子）
        
        // 正向延伸（dx[k], dy[k]方向）
        int i = x + dx[k], j = y + dy[k];
        while (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == color) { 
            count++; 
            i += dx[k]; 
            j += dy[k]; 
        }
        
        // 反向延伸（-dx[k], -dy[k]方向）
        i = x - dx[k]; 
        j = y - dy[k];
        while (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == color) { 
            count++; 
            i -= dx[k]; 
            j -= dy[k]; 
        }
        
        // 连续数≥5则判定胜利
        if (count >= 5) return true;
    }
    return false;
}

// --- 蒙特卡洛树搜索（MCTS）节点结构定义 ---
/**
 * @brief MCTS树节点结构：每个节点代表一次落子动作及对应的状态
 */
struct Node {
    int move_x, move_y; // 该节点对应的落子坐标
    int color;          // 落子方颜色（1=我方，-1=对方）
    int visits;         // N：节点总访问次数（用于UCB计算）
    double wins;        // W：节点累计胜利分数（模拟结果的累加）
    Node* parent;       // 父节点指针（回溯时使用）
    vector<Node*> children; // 子节点集合（已扩展的落子动作）
    vector<pair<int, int>> untriedMoves; // 未探索的合法落子点（扩展阶段使用），存储pair<int, int>（坐标对，代表棋盘(x,y)）的动态数组；

    /**
     * @brief 节点构造函数
     * @param x 落子横坐标
     * @param y 落子纵坐标
     * @param c 落子方颜色
     * @param p 父节点指针
     */
    Node(int x, int y, int c, Node* p) : 
        move_x(x), move_y(y), color(c), visits(0), wins(0), parent(p) {}

    /**
     * @brief 节点析构函数：递归释放所有子节点内存（防止内存泄漏）
     */
    ~Node() {
        for (Node* child : children) delete child;
    }
};

// --- MCTS 核心逻辑实现 ---

/**
 * @brief 获取当前棋盘所有合法落子点（优化版：仅返回有相邻落子的空位）
 * @return 合法落子坐标的列表
 */
vector<pair<int, int>> getLegalMoves() {
    vector<pair<int, int>> moves;
    for (int i = 0; i < SIZE; i++) {
        for (int j = 0; j < SIZE; j++) {
            // 空位 且 周围1格内有落子（大幅减少候选点数量，提升MCTS效率）
            if (board[i][j] == 0 && hasNeighbor(i, j, 1)) {
                moves.push_back({i, j});
            }
        }
    }
    return moves;
}

/**
 * @brief MCTS选择阶段（Tree Policy）：从树根出发，顺着已经建好的节点往下走，走到一个“还没完全探索完”的边缘节点。
 *                                   UCB1算法选择最优子节点，直到找到可扩展节点
 * @param node 当前节点
 * @return 选中的叶子节点（可扩展/游戏结束）
 */
Node* treePolicy(Node* node) {
    // 循环条件：节点无未探索动作 且 有子节点 → 继续向下选择
    while (node->untriedMoves.empty() && !node->children.empty()) {
        Node* bestChild = nullptr;
        double bestUCB = -9999999; // 初始化为极小值
        
        // 遍历所有子节点，计算UCB1值并选择最优
        for (Node* child : node->children) {
            // UCB1公式：利用项（胜率） + 探索项（平衡探索与利用）
            // 利用项：child->wins / child->visits （该子节点的胜率）
            // 探索项：1.414（√2，经验值） * sqrt(ln(父节点访问数) / 子节点访问数)
            double ucb = (child->wins / child->visits) + 
                         1.414 * sqrt(log(node->visits) / child->visits);
            
            // 记录UCB值最大的子节点
            if (ucb > bestUCB) {
                bestUCB = ucb;
                bestChild = child;
            }
        }
        
        // 移动到最优子节点
        node = bestChild;
        // 模拟落子：更新棋盘状态（treePolicy会修改棋盘，后续需恢复）
        board[node->move_x][node->move_y] = node->color;
    }
    return node; // 返回选中的叶子节点
}

/**
 * @brief MCTS扩展阶段（Expand）：为节点扩展一个未探索的子节点。
 * 当走到了边缘节点，发现 untriedMoves（还没试过的空位）里还有存货时，随机掏出一个空位，真正地在内存里 new Node（长出一根新树枝）。
 * @param node 待扩展的叶子节点
 * @return 新扩展的子节点（若无可扩展动作则返回原节点）
 */
Node* expand(Node* node) {
    if (node->untriedMoves.empty()) return node; // 第一次迭代不触发
    
    // 1. 随机选一个未探索的落子（比如开局选中心(7,7)）
    int idx = rand() % node->untriedMoves.size();
    pair<int, int> move = node->untriedMoves[idx];
    
    // 2. 把这个落子从未探索列表移除（标记为已探索）
    node->untriedMoves[idx] = node->untriedMoves.back();
    node->untriedMoves.pop_back();

    // 3. 创建新子节点：颜色是对方（-1→1），父节点是root
    int nextColor = (node->color == 1) ? -1 : 1; // node=root（color=-1）→ nextColor=1（我方）
    Node* child = new Node(move.first, move.second, nextColor, node);
    
    // 4. 初始化子节点的未探索落子（基于当前棋盘生成）
    child->untriedMoves = getLegalMoves();
    // 5. 把新子节点加入根节点的children（root终于有第一个子节点了）
    node->children.push_back(child);
    
    // 6. 模拟落子（修改全局棋盘）
    board[child->move_x][child->move_y] = child->color;
    return child; // 返回这个新生成的子节点
}

/**
 * @brief MCTS模拟阶段（Rollout/Simulation）：随机落子直到游戏结束或达到步数上限
 * @param currentColor 当前落子方颜色
 * @return 模拟结果（1=我方赢，0=对方赢，0.5=平局）
 */
double simulate(int currentColor) {
    vector<pair<int, int>> playedMoves; // 记录模拟过程的落子，用于恢复棋盘
    int turn = currentColor; // 当前轮次的落子方
    double result = 0.5;     // 默认平局（得分0.5）
    
    // 步数上限：最多模拟30步（防止无限循环，平衡效率与准确性）
    for (int step = 0; step < 30; step++) {
        // 获取当前合法落子点
        vector<pair<int, int>> legalMoves = getLegalMoves();
        if (legalMoves.empty()) break; // 棋盘满，平局退出
        
        // 随机选择一个落子点（纯随机模拟，也可优化为启发式模拟）
        pair<int, int> move = legalMoves[rand() % legalMoves.size()];
        board[move.first][move.second] = turn;
        playedMoves.push_back(move); // 记录落子
        
        // 检查是否赢棋
        if (checkWinFast(move.first, move.second, turn)) {
            // 我方赢则返回1.0，对方赢则返回0.0
            result = (turn == 1) ? 1.0 : 0.0;
            break; // 模拟结束
        }
        // 交换落子方
        turn = (turn == 1) ? -1 : 1;
    }

    // 恢复棋盘：撤销模拟阶段的所有落子
    for (auto move : playedMoves) {
        board[move.first][move.second] = 0;
    }
    return result;
}

/**
 * @brief MCTS回溯阶段（Backpropagation）：将模拟结果反向更新到所有祖先节点
 * @param node 模拟结束的节点
 * @param result 模拟结果（1.0/0.5/0.0）
 */
void backpropagate(Node* node, double result) {
    // 从当前节点向上遍历到根节点
    while (node != nullptr) {
        node->visits++; // 节点访问次数+1
        
        // 胜负分数更新：视角转换（我方节点记录我方胜率，对方节点记录对方胜率）
        if (node->color == 1) {
            // 我方节点：直接累加模拟结果（1=赢，0=输，0.5=平）
            node->wins += result; 
        } else {
            // 对方节点：累加反向结果（对方赢则我方输，得分1-result）
            node->wins += (1.0 - result);
        }
        
        // 移动到父节点
        node = node->parent;
    }
}

/**
 * @brief MCTS总调度函数：执行MCTS四阶段（选择→扩展→模拟→回溯），返回最优落子
 * @return 最优落子坐标（x,y）
 */
pair<int, int> getBestMoveMCTS() {
    // 创建根节点：坐标(-1,-1)（无落子），颜色-1（代表下一轮是我方1落子），无父节点
    Node* root = new Node(-1, -1, -1, nullptr);
    root->untriedMoves = getLegalMoves(); // 根节点初始化未探索动作，所有合法落子点都是未探索的

    clock_t start_time = clock(); // 记录开始时间（用于卡时）
    int iterations = 0;           // 迭代次数统计（可选，用于调试）

    // 核心循环：在时间限制内反复执行MCTS四阶段
    while (clock() - start_time < TIME_LIMIT) {
        // 1. 选择阶段：通过UCB1选择叶子节点（会修改棋盘状态）
        Node* leaf = treePolicy(root); 
        // 2. 扩展阶段：扩展叶子节点（若有未探索动作）
        Node* expanded = expand(leaf);
        // 3. 模拟阶段：从扩展节点的下一方开始随机模拟；
        // expanded是刚生成的子节点（color=1，我方），所以模拟从对方（-1）开始
        double result = simulate((expanded->color == 1) ? -1 : 1); 
        // 4. 回溯阶段：更新节点统计信息
        backpropagate(expanded, result);

        // 恢复棋盘到根节点状态（撤销treePolicy和expand的落子）
        Node* temp = expanded;
        while (temp != root) {
            board[temp->move_x][temp->move_y] = 0;
            temp = temp->parent;
        }

        iterations++; // 迭代次数+1
    }

    // 时间耗尽：选择访问次数最多的子节点作为最优落子（MCTS经典策略）
    Node* bestChild = nullptr;
    int maxVisits = -1;
    for (Node* child : root->children) {
        if (child->visits > maxVisits) {
            maxVisits = child->visits;
            bestChild = child;
        }
    }

    // 记录最优落子坐标
    pair<int, int> bestMove = {bestChild->move_x, bestChild->move_y};
    delete root; // 释放整棵MCTS树的内存
    return bestMove;
}

/**
 * @brief 主函数：处理输入（对手落子），调用MCTS决策，输出最优落子
 */
int main() {
    srand(time(0)); // 初始化随机数种子（保证模拟阶段的随机性）
    
    int x, y, n;
    cin >> n; // 输入总回合数
    
    // 读取前n-1回合的落子记录（对方→我方 交替）
    for (int i = 0; i < n - 1; i++) {
        cin >> x >> y; 
        if (x != -1) board[x][y] = -1; // 对方落子
        
        cin >> x >> y; 
        if (x != -1) board[x][y] = 1;  // 我方落子
    }
    
    // 读取当前回合对方的落子
    cin >> x >> y;
    if (x != -1) board[x][y] = -1;

    int best_x = -1, best_y = -1; // 最终输出的落子坐标

    // 特殊情况处理1：首次落子且对方已下（换手，返回-1,-1）
    if (x != -1 && n == 1) {  
        best_x = -1; 
        best_y = -1; 
    } 
    // 特殊情况处理2：开局我方先下（无对方落子），直接下棋盘中心
    else if (n == 1 && x == -1) {
        best_x = SIZE / 2; 
        best_y = SIZE / 2; 
    } 
    // 正常情况：调用MCTS获取最优落子
    else {
        pair<int, int> bestMove = getBestMoveMCTS();
        best_x = bestMove.first;
        best_y = bestMove.second;
    }

    // 输出最优落子坐标
    printf("%d %d\n", best_x, best_y);
    return 0;
}