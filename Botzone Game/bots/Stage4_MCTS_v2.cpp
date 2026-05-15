#include <iostream>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <algorithm>

using namespace std;

// ==========================================
// 【参数与全局状态】
// ==========================================

const int SIZE = 15;
// 卡时阈值：Botzone 平台严格限制1秒，保守设定为0.90秒
const double TIME_LIMIT = 0.90 * CLOCKS_PER_SEC; 

// 全局棋盘：0=空，1=我方落子，-1=对方落子
int board[SIZE][SIZE] = { 0 };

// ==========================================
// 【基础打分与评估系统 (复用贪心阶段的专家知识)】
// ==========================================

// 连子得分表（极度偏好连五和活四）
int getScore(int count, int open_ends) {
    if (count >= 5) return 100000;
    if (count == 4) return (open_ends == 2) ? 10000 : 1000;
    if (count == 3) return (open_ends == 2) ? 1000 : 100;
    if (count == 2) return (open_ends == 2) ? 100 : 10;
    return 0;
}

// 单方向得分统计
int countDirectionScore(int x, int y, int dx, int dy, int color) {
    int count = 1, open_ends = 0;
    int i = x + dx, j = y + dy;
    while (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == color) { count++; i += dx; j += dy; }
    if (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == 0) open_ends++;
    
    i = x - dx; j = y - dy;
    while (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == color) { count++; i -= dx; j -= dy; }
    if (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == 0) open_ends++;
    
    return getScore(count, open_ends);
}

// 评估单个点位对特定颜色的价值
int evaluatePoint(int x, int y, int color) {
    int score = 0;
    int dx[4] = {1, 0, 1, 1}, dy[4] = {0, 1, 1, -1};
    for (int k = 0; k < 4; k++) {
        score += countDirectionScore(x, y, dx[k], dy[k], color);
    }
    return score;
}

// 综合评估（我方进攻分 + 堵截对方的防守分）
int evaluatePointTotal(int x, int y) {
    return evaluatePoint(x, y, 1) + evaluatePoint(x, y, -1);
}

// 评估全局棋盘分数 (用于 MCTS 截断模拟阶段)
int evaluateBoard(int myColor) {
    int myScore = 0, oppScore = 0;
    for (int i = 0; i < SIZE; i++) {
        for (int j = 0; j < SIZE; j++) {
            if (board[i][j] == myColor) myScore += evaluatePoint(i, j, myColor);
            else if (board[i][j] != 0) oppScore += evaluatePoint(i, j, -myColor);
        }
    }
    return myScore - oppScore;
}

// 检查是否有邻居（剪枝优化）
bool hasNeighbor(int x, int y, int radius = 1) {
    for (int i = max(0, x - radius); i <= min(SIZE - 1, x + radius); i++) {
        for (int j = max(0, y - radius); j <= min(SIZE - 1, y + radius); j++) {
            if (i == x && j == y) continue;
            if (board[i][j] != 0) return true;
        }
    }
    return false;
}

// 快速检查胜利
bool checkWinFast(int x, int y, int color) {
    int dx[4] = {1, 0, 1, 1}, dy[4] = {0, 1, 1, -1};
    for (int k = 0; k < 4; k++) {
        int count = 1;
        int i = x + dx[k], j = y + dy[k];
        while (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == color) { count++; i += dx[k]; j += dy[k]; }
        i = x - dx[k]; j = y - dy[k];
        while (i >= 0 && i < SIZE && j >= 0 && j < SIZE && board[i][j] == color) { count++; i -= dx[k]; j -= dy[k]; }
        if (count >= 5) return true;
    }
    return false;
}

// ==========================================
// 【学习记录：混合引擎架构 - 外部拦截网】
// 目标：条件反射！MCTS 启动前，先检查有没有“立刻赢”或“立刻死”的棋。
// ==========================================
pair<int, int> getUrgentMove() {
    pair<int, int> bestDefend = {-1, -1};
    int maxDefendScore = -1;

    for (int i = 0; i < SIZE; i++) {
        for (int j = 0; j < SIZE; j++) {
            if (board[i][j] == 0 && hasNeighbor(i, j, 2)) {
                // 1. 必杀检查：我方下这步能直接赢吗？（活四或连五）
                if (evaluatePoint(i, j, 1) >= 10000) return {i, j}; 

                // 2. 必防检查：对方下这步能直接赢吗？
                int oppScore = evaluatePoint(i, j, -1);
                if (oppScore >= 10000) {
                    if (oppScore > maxDefendScore) {
                        maxDefendScore = oppScore;
                        bestDefend = {i, j};
                    }
                }
            }
        }
    }
    // 如果有致命威胁，优先防守
    if (bestDefend.first != -1) return bestDefend;
    
    // 无紧急情况，返回无效坐标交由 MCTS 处理
    return {-1, -1}; 
}

// ==========================================
// 【学习记录：MCTS 节点与启发式扩展 (Expand)】
// ==========================================

// 用于排序的结构体
struct MoveInfo {
    int x, y, score;
    bool operator<(const MoveInfo& other) const { return score < other.score; }
};

/**
 * @brief 启发式动作生成器（已修复编译警告）
 * 1. 估值打分  2. 升序排序  3. Top-K 截断
 */
vector<pair<int, int>> getHeuristicLegalMoves() {
    vector<MoveInfo> candidateMoves;
    for (int i = 0; i < SIZE; i++) {
        for (int j = 0; j < SIZE; j++) {
            if (board[i][j] == 0 && hasNeighbor(i, j, 1)) {
                candidateMoves.push_back({i, j, evaluatePointTotal(i, j)});
            }
        }
    }
    sort(candidateMoves.begin(), candidateMoves.end());
    
    // 【编译警告修复】：强制转为 int 并在有符号域计算，防止越界
    int totalMoves = (int)candidateMoves.size(); 
    int keepCount = min(totalMoves, 15); // Top-K 截断：最多保留前 15 步好棋
    vector<pair<int, int>> finalMoves;
    
    for (int i = totalMoves - keepCount; i < totalMoves; i++) {
        finalMoves.push_back({candidateMoves[i].x, candidateMoves[i].y});
    }
    return finalMoves;
}

struct Node {
    int move_x, move_y; 
    int color;          
    int visits;         
    double wins;        
    Node* parent;       
    vector<Node*> children; 
    vector<pair<int, int>> untriedMoves; 
    Node(int x, int y, int c, Node* p) : move_x(x), move_y(y), color(c), visits(0), wins(0), parent(p) {}
    ~Node() { for (Node* child : children) delete child; }
};

// ==========================================
// MCTS 核心逻辑
// ==========================================

Node* treePolicy(Node* node) {
    while (node->untriedMoves.empty() && !node->children.empty()) {
        Node* bestChild = nullptr;
        double bestUCB = -9999999; 
        for (Node* child : node->children) {
            double ucb = (child->visits == 0) ? 9999999 : 
                         (child->wins / child->visits) + 1.414 * sqrt(log(node->visits) / child->visits);
            if (ucb > bestUCB) { bestUCB = ucb; bestChild = child; }
        }
        node = bestChild;
        board[node->move_x][node->move_y] = node->color;
    }
    return node;
}

Node* expand(Node* node) {
    if (node->untriedMoves.empty()) return node; 
    // 【学习记录：启发式引导】由于 untriedMoves 已经排序，back() 就是评分最高的动作
    pair<int, int> move = node->untriedMoves.back();
    node->untriedMoves.pop_back();

    int nextColor = (node->color == 1) ? -1 : 1; 
    Node* child = new Node(move.first, move.second, nextColor, node);
    child->untriedMoves = getHeuristicLegalMoves();
    
    // 【学习记录：先验知识注入】给这步好棋加点“初始胜场”，让 UCB 公式偏爱它
    int initialScore = evaluatePointTotal(move.first, move.second);
    child->wins += min(1.0, (double)initialScore / 10000.0);

    node->children.push_back(child);
    board[child->move_x][child->move_y] = child->color;
    return child;
}

// 【学习记录：启发式截断与 Sigmoid 概率映射 (Simulate)】
double simulate(int currentColor) {
    vector<pair<int, int>> playedMoves; 
    int turn = currentColor; 
    double result = -1.0; // -1 代表尚未分出胜负
    
    // 1. 启发式截断：放弃瞎下 30 步，只往下短距推演 8 步，大幅提升迭代速度！
    for (int step = 0; step < 8; step++) {
        vector<pair<int, int>> legalMoves;
        // 模拟阶段不排序，追求极速
        for (int i = 0; i < SIZE; i++) {
            for (int j = 0; j < SIZE; j++) {
                if (board[i][j] == 0 && hasNeighbor(i, j, 1)) legalMoves.push_back({i, j});
            }
        }
        if (legalMoves.empty()) break; 
        
        pair<int, int> move = legalMoves[rand() % legalMoves.size()];
        board[move.first][move.second] = turn;
        playedMoves.push_back(move); 
        
        if (checkWinFast(move.first, move.second, turn)) {
            result = (turn == 1) ? 1.0 : 0.0;
            break; 
        }
        turn = (turn == 1) ? -1 : 1;
    }

    // 2. 静态评估与映射：8 步内没死人？那就叫停，用人类知识打分！
    if (result < 0) {
        int finalScore = evaluateBoard(1); // 站在我方(1)的视角看当前盘面
        // Sigmoid 映射：将庞大的盘面分差平滑压缩成 [0,1] 的胜率
        double k = 0.005; 
        result = 1.0 / (1.0 + exp(-k * finalScore));
    }

    // 恢复现场
    for (auto move : playedMoves) board[move.first][move.second] = 0;
    return result;
}

void backpropagate(Node* node, double result) {
    while (node != nullptr) {
        node->visits++; 
        if (node->color == 1) node->wins += result; 
        else node->wins += (1.0 - result);
        node = node->parent;
    }
}

pair<int, int> getBestMoveMCTS() {
    // 【学习记录：外部拦截】在烧算力之前，先靠“条件反射”扫描必胜/必死点
    pair<int, int> urgentMove = getUrgentMove();
    if (urgentMove.first != -1) return urgentMove; // 发现致命威胁，立刻反击！

    Node* root = new Node(-1, -1, -1, nullptr);
    root->untriedMoves = getHeuristicLegalMoves(); 

    clock_t start_time = clock(); 
    while (clock() - start_time < TIME_LIMIT) {
        Node* leaf = treePolicy(root); 
        Node* expanded = expand(leaf);
        double result = simulate((expanded->color == 1) ? -1 : 1); 
        backpropagate(expanded, result);

        Node* temp = expanded;
        while (temp != root) {
            board[temp->move_x][temp->move_y] = 0;
            temp = temp->parent;
        }
    }

    // MCTS 决策：挑选被访问次数最多（经受住考验）的分支
    Node* bestChild = nullptr;
    int maxVisits = -1;
    for (Node* child : root->children) {
        if (child->visits > maxVisits) { maxVisits = child->visits; bestChild = child; }
    }

    if (bestChild == nullptr) {
        delete root;
        if (!root->untriedMoves.empty()) return root->untriedMoves.back();
        return {SIZE/2, SIZE/2}; 
    }

    pair<int, int> bestMove = {bestChild->move_x, bestChild->move_y};
    delete root; 
    return bestMove;
}

int main() {
    srand(time(0)); 
    int x, y, n;
    cin >> n; 
    
    // 【学习记录：解决所有权翻转 Bug】
    // Botzone Swap1 规则下，即便发生换手，输入坐标序列的顺序不变。
    // 这段逻辑天然适应，不论身份如何变换，程序只关注“-1为对方，1为我方”。
    for (int i = 0; i < n - 1; i++) {
        cin >> x >> y; if (x != -1) board[x][y] = -1; 
        cin >> x >> y; if (x != -1) board[x][y] = 1;  
    }
    
    cin >> x >> y;
    if (x != -1) board[x][y] = -1;

    int best_x = -1, best_y = -1; 

    // 一手交换规则：后手第一回合强制换手（防御平台黑棋天胡开局）
    if (x != -1 && n == 1) {  
        best_x = -1; best_y = -1; 
    } else if (n == 1 && x == -1) {
        best_x = SIZE / 2; best_y = SIZE / 2; // 空盘直接占天元
    } else {
        pair<int, int> bestMove = getBestMoveMCTS();
        best_x = bestMove.first;
        best_y = bestMove.second;
    }

    printf("%d %d\n", best_x, best_y);
    return 0;
}