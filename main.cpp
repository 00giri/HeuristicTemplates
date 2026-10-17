#include <bits/stdc++.h>
using namespace std;
using ll = long long;
#define rep(i, s, n) for (int i = (s); i < (int)(n); i++)
#define all(v) v.begin(), v.end()

// 使い方・設定項目・使用例は README.md を参照。

namespace heuristic {

using u64 = uint64_t;
using i64 = int64_t;
using u128 = __uint128_t;
using i128 = __int128_t;

// ---------- 乱数 ----------

// Mwc256XXA64 のアルゴリズムと初期化の参照先:
// https://github.com/tkaitchuck/Mwc256XXA64/blob/main/impl/src/gen64.rs
// 64 ビット×64 ビットの積の「加算前の上位」を出力の攪拌に使う点に注意。
class Mwc256XXA64 {
    static constexpr u64 multiplier = 0xfeb344657c0af413ULL;
    u64 x1_, x2_, x3_, carry_;

public:
    using result_type = u64;

    explicit Mwc256XXA64(u64 value = 1) { seed(value); }
    Mwc256XXA64(u64 key1, u64 key2) { seed(key1, key2); }

    void seed(u64 value) { seed(value, value); }

    void seed(u64 key1, u64 key2) {
        x1_ = key1;
        x2_ = key2;
        x3_ = 0xcafef00dd15ea5e5ULL;
        carry_ = 0x14057b7ef767814fULL;
        // seed が 0 でも禁止状態に入らない。原実装と同じ 6 回の空回し。
        for (int i = 0; i < 6; ++i) next_u64();
    }

    u64 next_u64() {
        const u128 product = u128(x3_) * multiplier;
        const u64 high = u64(product >> 64);
        const u64 result = (x3_ ^ x2_) + (x1_ ^ high);
        const u128 next = product + carry_;
        x3_ = x2_;
        x2_ = x1_;
        x1_ = u64(next);
        carry_ = u64(next >> 64);
        return result;
    }

    // 標準の乱数生成器として shuffle などにも渡せる。
    u64 operator()() { return next_u64(); }
    static constexpr u64 min() { return 0; }
    static constexpr u64 max() { return numeric_limits<u64>::max(); }

    // [0, n)、n > 0。除算・棄却なしの高速版。
    // 各値に対応する元の乱数の個数に最大 1 個の差がある。小さな n 向け。
    // n が 2 の冪なら偏りなし。それ以外で厳密な一様性が必要なら uniform。
    u64 fast(u64 n) {
        assert(n > 0);
        return u64((u128(next_u64()) * n) >> 64);
    }

    // [0, n)、n > 0。元の 64 ビット値が一様なら、変換による偏りを生じない。
    // Lemire 法。小さな n では、ほぼ常に除算も再抽選も発生しない。
    // https://arxiv.org/abs/1805.10941
    u64 uniform(u64 n) {
        assert(n > 0);
        u128 product = u128(next_u64()) * n;
        u64 low = u64(product);
        if (low < n) {
            const u64 threshold = (u64(0) - n) % n;
            while (low < threshold) {
                product = u128(next_u64()) * n;
                low = u64(product);
            }
        }
        return u64(product >> 64);
    }

    // [0, 1) の double。上位 53 ビットを使い、丸めによる 1.0 を避ける。
    double uniform01() { return double(next_u64() >> 11) * 0x1.0p-53; }
};

// ---------- 共通の時間管理 ----------

class TimeBudget {
    using Clock = chrono::steady_clock;
    Clock::time_point start_ = Clock::now();
    double seconds_;

public:
    // 省略時は時間無制限。固定回数での実験やテストに使う。
    explicit TimeBudget(double seconds = numeric_limits<double>::infinity())
        : seconds_(seconds) {
        assert(seconds >= 0.0);
    }

    double elapsed() const {
        return chrono::duration<double>(Clock::now() - start_).count();
    }
    double limit() const { return seconds_; }
    bool limited() const { return isfinite(seconds_); }
    bool expired() const { return limited() && elapsed() >= seconds_; }
};

namespace detail {

// 毎回時計を読むコストを避ける。呼び出し回数と時計確認間隔を分離する。
class ClockCheck {
    const TimeBudget& budget_;
    uint32_t interval_, remaining_ = 0;
    bool stopped_ = false;

public:
    ClockCheck(const TimeBudget& budget, uint32_t interval)
        : budget_(budget), interval_(interval) {
        assert(interval > 0);
    }
    bool now() { return stopped_ = stopped_ || budget_.expired(); }
    bool poll() {
        if (stopped_) return true;
        if (remaining_ == 0) {
            remaining_ = interval_;
            now();
        }
        --remaining_;
        return stopped_;
    }
};

template<class Score>
double difference(Score next, Score current) {
    // int64_t の減算オーバーフローや、先に double に変換する桁落ちを防ぐ。
    if constexpr (is_integral_v<Score>) {
        return double(i128(next) - i128(current));
    } else {
        return double(next - current);
    }
}

}  // namespace detail

// ---------- 焼きなまし法 ----------

struct AnnealingConfig {
    double start_temperature = 100.0;
    double end_temperature = 1.0;
    u64 max_iterations = numeric_limits<u64>::max();
    uint32_t update_interval = 64;  // 時計と温度を更新する試行間隔
};

template<class State>
struct AnnealingResult {
    State state;                       // 探索中に見つけた最良解
    typename State::Score score;
    u64 iterations = 0;
    u64 accepted = 0;
};

// 状態のコピーは最良解を更新したときだけ。棄却時は Undo で差分を戻す。
// 時間と試行回数の両方を指定した場合、先に達する制限で終了する。
template<class State>
AnnealingResult<State> simulated_annealing(
    State current, Mwc256XXA64& rng, const TimeBudget& budget,
    const AnnealingConfig& config = {}) {
    using Score = typename State::Score;
    assert(config.start_temperature > 0 && isfinite(config.start_temperature));
    assert(config.end_temperature > 0 && isfinite(config.end_temperature));
    assert(config.update_interval > 0);
    assert(budget.limited() || config.max_iterations != numeric_limits<u64>::max());

    Score current_score = current.score();
    AnnealingResult<State> result{current, current_score};
    const double start_time = budget.limited() ? budget.elapsed() : 0.0;
    const double available = budget.limit() - start_time;
    if (available <= 0.0) return result;
    const double log_start = log(config.start_temperature);
    const double log_end = log(config.end_temperature);
    double temperature = config.start_temperature;
    uint32_t until_update = 0;

    for (u64 i = 0; i < config.max_iterations; ++i) {
        if (until_update == 0) {
            double progress = 0.0;
            if (budget.limited()) {
                const double elapsed = budget.elapsed();
                if (elapsed >= budget.limit()) break;
                progress = (elapsed - start_time) / available;
            }
            if (config.max_iterations != numeric_limits<u64>::max()) {
                const double by_count = config.max_iterations > 1
                    ? double(i) / double(config.max_iterations - 1) : 0.0;
                progress = max(progress, by_count);
            }
            // 幾何冷却。高価な log / exp を毎試行の温度更新に使わない。
            temperature = exp(log_start + (log_end - log_start) * progress);
            until_update = config.update_interval;
        }
        --until_update;
        const auto move = current.propose(rng);
        if (!move) break;
        const auto undo = current.apply(*move);
        const Score next_score = current.score();
        const double delta = detail::difference(next_score, current_score);
        ++result.iterations;

        if (delta >= 0.0 || rng.uniform01() < exp(delta / temperature)) {
            current_score = next_score;
            ++result.accepted;
            if (next_score > result.score) {
                result.state = current;
                result.score = next_score;
            }
        } else {
            current.undo(*move, undo);
        }
    }
    return result;
}

// ---------- ビームサーチ共通 ----------

struct BeamConfig {
    int width = 100;
    int max_depth = 100;
    bool deduplicate = false;
    uint32_t time_check_interval = 256;
};

template<class State>
struct BeamResult {
    State state;
    vector<typename State::Move> moves;
    typename State::Score score;
    int depth = 0;
    bool timed_out = false;
};

namespace detail {

template<class Move, class Score>
struct Candidate {
    Move move;
    Score score;
    u64 hash;
    int parent;                        // 前の層での順位
    size_t order;                      // 生成順（木上版では全候補を通した番号）
};

template<class C>
bool better(const C& a, const C& b) {
    if (a.score != b.score) return a.score > b.score;
    if (a.parent != b.parent) return a.parent < b.parent;
    return a.order < b.order;
}

// 重複なしなら平均 O(候補数 + 幅 log 幅)。状態を持たない軽い候補だけを選抜。
// 重複除去ありでは、まずハッシュで整列するため O(候補数 log 候補数)。
// 同点は親の順位と生成順で決め、通常版と木上版で同じ結果になるようにする。
template<class C>
void select_candidates(vector<C>& candidates, const BeamConfig& config) {
    if (config.deduplicate) {
        sort(candidates.begin(), candidates.end(), [](const C& a, const C& b) {
            if (a.hash != b.hash) return a.hash < b.hash;
            return better(a, b);
        });
        candidates.erase(unique(candidates.begin(), candidates.end(),
            [](const C& a, const C& b) { return a.hash == b.hash; }), candidates.end());
    }
    if (candidates.size() > size_t(config.width)) {
        const auto cut = candidates.begin() + config.width;
        nth_element(candidates.begin(), cut, candidates.end(), better<C>);
        candidates.erase(cut, candidates.end());
    }
    sort(candidates.begin(), candidates.end(), better<C>);
}

template<class Move>
struct History {
    int parent;
    Move move;
};

template<class Move>
vector<Move> restore_path(const vector<History<Move>>& history, int id) {
    vector<Move> path;
    for (; id != -1; id = history[id].parent) path.push_back(history[id].move);
    reverse(path.begin(), path.end());
    return path;
}

}  // namespace detail

// ---------- 通常のビームサーチ ----------

// 採用した候補についてのみ親状態をコピーする。
// 状態のメモリ O(幅×State)、復元履歴 O(幅×深さ)、一時候補 O(幅×分岐数)。
template<class State>
BeamResult<State> beam_search(State initial, const TimeBudget& budget,
                             const BeamConfig& config = {}) {
    using Move = typename State::Move;
    using Score = typename State::Score;
    using Candidate = detail::Candidate<Move, Score>;
    assert(config.width > 0 && config.max_depth >= 0);
    struct Entry { State state; Score score; int history; };
    const Score initial_score = initial.score();
    vector<Entry> beam;
    beam.push_back({move(initial), initial_score, -1});
    vector<Entry> next;
    next.reserve(config.width);
    vector<detail::History<Move>> history;
    vector<Candidate> candidates;
    candidates.reserve(size_t(config.width) * 8);
    detail::ClockCheck clock(budget, config.time_check_interval);
    int depth = 0;
    bool timed_out = false;

    while (depth < config.max_depth) {
        if (clock.now()) { timed_out = true; break; }
        candidates.clear();
        for (int parent = 0; parent < int(beam.size()); ++parent) {
            if (clock.poll()) break;
            size_t order = 0;
            beam[parent].state.expand([&](const Move& move, Score score, u64 hash = 0) {
                if (clock.poll()) return false;
                candidates.push_back({move, score, hash, parent, order++});
                return true;
            });
        }
        // 未完成の層を採用すると列挙順に偏るので、直前の完成層を返す。
        if (clock.now()) { timed_out = true; break; }
        if (candidates.empty()) break;
        detail::select_candidates(candidates, config);
        if (clock.now()) { timed_out = true; break; }

        next.clear();
        for (const auto& candidate : candidates) {
            if (clock.poll()) break;
            State state = beam[candidate.parent].state;
            state.apply(candidate.move);
            const int id = int(history.size());
            history.push_back({beam[candidate.parent].history, candidate.move});
            next.push_back({move(state), candidate.score, id});
        }
        if (clock.now()) { timed_out = true; break; }
        beam.swap(next);
        ++depth;
    }
    auto moves = detail::restore_path(history, beam.front().history);
    const Score score = beam.front().score;
    return {move(beam.front().state), move(moves), score, depth, timed_out};
}

// ---------- 木上のビームサーチ ----------

namespace detail {

// 隣り合う葉の区間長は、その 2 葉から LCA までの距離。
// 複数の葉を飛ばす場合の距離は、飛ばす区間長の最大値になる。
inline int postorder_distance(const vector<size_t>& offsets, int first, int last) {
    size_t distance = 0;
    for (int i = first; i < last; ++i) {
        distance = max(distance, offsets[i + 1] - offsets[i]);
    }
    return int(distance);
}

// 現在の葉から左側の葉への経路を復元する。葉側から確定した部分は上書きしない。
template<class Move>
void postorder_rewrite(vector<Move>& trace, const vector<Move>& tour,
                       const vector<size_t>& offsets, int first, int last, int height) {
    int filled = 0;
    for (int i = first; i < last; ++i) {
        const int length = int(offsets[i + 1] - offsets[i]);
        if (length > filled) {
            copy_n(tour.begin() + offsets[i], length - filled, trace.begin() + height - length);
            filled = length;
        }
    }
}

}  // 内部実装

// 帰りがけ順の連続配列で経路を共有し、状態を最後の葉に保持する。
// 参考: https://trap.jp/post/2920/
// 各層で根へ戻らず、次の葉との LCA までだけ巻き戻す。共通部分は動かさない。
// 採用した枝だけを次の配列に詰め直すので、リンクや空き頂点の管理は不要。
// 状態 O(State)、経路配列は最悪 O(幅×深さ)、Undo は O(深さ)。再帰なし。
template<class State>
BeamResult<State> tree_beam_search(State state, const TimeBudget& budget,
                                  const BeamConfig& config = {}) {
    using Move = typename State::Move;
    using Score = typename State::Score;
    using Undo = typename State::Undo;
    using Candidate = detail::Candidate<Move, Score>;
    assert(config.width > 0 && config.max_depth >= 0);
    detail::ClockCheck clock(budget, config.time_check_interval);
    const Score initial_score = state.score();
    if (config.max_depth == 0) return {move(state), {}, initial_score, 0, false};
    if (clock.now()) return {move(state), {}, initial_score, 0, true};

    // 幅 1 では葉の間を移動しない。経路の配列だけを持ち、Undo も保存しない。
    if (config.width == 1) {
        vector<Move> path;
        Score current_score = initial_score;
        int depth = 0;
        bool timed_out = false;
        while (depth < config.max_depth) {
            if (clock.now()) { timed_out = true; break; }
            optional<Move> best;
            Score best_score = current_score;
            state.expand([&](const Move& action, Score score, u64 = 0) {
                if (clock.poll()) return false;
                if (!best || score > best_score) {
                    best.emplace(action);
                    best_score = score;
                }
                return true;
            });
            if (clock.now()) { timed_out = true; break; }
            if (!best) break;
            state.apply(*best);
            path.push_back(*best);
            current_score = best_score;
            ++depth;
        }
        return {move(state), move(path), current_score, depth, timed_out};
    }

    vector<Candidate> candidates, next_candidates;
    candidates.reserve(size_t(config.width) * 8);
    next_candidates.reserve(size_t(config.width) * 8);
    state.expand([&](const Move& action, Score score, u64 hash = 0) {
        if (clock.poll()) return false;
        candidates.push_back({action, score, hash, 0, candidates.size()});
        return true;
    });
    if (clock.now()) return {move(state), {}, initial_score, 0, true};
    if (candidates.empty()) return {move(state), {}, initial_score, 0, false};
    detail::select_candidates(candidates, config);
    if (clock.now()) return {move(state), {}, initial_score, 0, true};

    vector<Move> trace, tour, next_tour;
    vector<Undo> undos;
    vector<size_t> offsets{0}, next_offsets;
    vector<int> rank_to_leaf{0}, next_rank_to_leaf, visit;
    int depth = 1;

    auto rollback = [&](int keep) {
        while (int(undos.size()) > keep) {
            state.undo(trace[undos.size() - 1], undos.back());
            undos.pop_back();
        }
    };
    // 未展開の最良候補にも移動できる。最後の完成層から解を返すために使う。
    auto finish = [&](const vector<Move>& saved_tour, const vector<size_t>& saved_offsets,
                      int target, int current, int height, const Move* action,
                      bool timed_out) -> BeamResult<State> {
        const int keep = height - detail::postorder_distance(saved_offsets, target, current);
        rollback(keep);
        trace.erase(trace.begin() + height, trace.end());
        detail::postorder_rewrite(trace, saved_tour, saved_offsets, target, current, height);
        for (int i = keep; i < height; ++i) state.apply(trace[i]);
        if (action) {
            state.apply(*action);
            trace.push_back(*action);
        }
        return {move(state), move(trace), candidates.front().score, depth, timed_out};
    };

    for (;;) {
        const int height = depth - 1;
        int current_leaf = int(offsets.size()) - 1;
        if (depth == config.max_depth || clock.now()) {
            return finish(tour, offsets, rank_to_leaf[candidates[0].parent], current_leaf,
                          height, &candidates[0].move, depth != config.max_depth);
        }
        next_tour.clear();
        next_offsets.clear();
        next_candidates.clear();
        next_rank_to_leaf.resize(candidates.size());
        visit.resize(candidates.size());
        iota(visit.begin(), visit.end(), 0);
        // 生成順の逆順に訪問すると、親の葉の番号が単調に減少する。
        sort(visit.begin(), visit.end(), [&](int a, int b) {
            return candidates[a].order > candidates[b].order;
        });
        int best_leaf = -1;
        auto finish_layer = [&](bool timed_out) {
            if (best_leaf != -1) {
                return finish(next_tour, next_offsets, best_leaf, int(next_offsets.size()) - 1,
                              depth, nullptr, timed_out);
            }
            return finish(tour, offsets, rank_to_leaf[candidates[0].parent], current_leaf,
                          height, &candidates[0].move, timed_out);
        };

        for (int rank : visit) {
            if (clock.poll()) break;
            const auto& candidate = candidates[rank];
            const int target = rank_to_leaf[candidate.parent];
            const int keep = height - detail::postorder_distance(offsets, target, current_leaf);
            if (!next_offsets.empty()) {
                next_tour.insert(next_tour.end(), trace.begin() + keep, trace.end());
            }
            rollback(keep);
            trace.erase(trace.begin() + height, trace.end());
            detail::postorder_rewrite(trace, tour, offsets, target, current_leaf, height);
            for (int i = keep; i < height; ++i) undos.push_back(state.apply(trace[i]));
            trace.push_back(candidate.move);
            undos.push_back(state.apply(candidate.move));
            current_leaf = target;

            const int leaf = int(next_offsets.size());
            next_rank_to_leaf[rank] = leaf;
            next_offsets.push_back(next_tour.size());
            if (rank == 0) best_leaf = leaf;
            state.expand([&](const Move& action, Score score, u64 hash = 0) {
                if (clock.poll()) return false;
                next_candidates.push_back({action, score, hash, rank, next_candidates.size()});
                return true;
            });
        }
        // 途中で止まっても、展開済み／未展開のどちらの最良候補へも復元できる。
        if (clock.now()) return finish_layer(true);
        if (next_candidates.empty()) return finish_layer(false);
        detail::select_candidates(next_candidates, config);
        if (clock.now()) return finish_layer(true);
        tour.swap(next_tour);
        offsets.swap(next_offsets);
        rank_to_leaf.swap(next_rank_to_leaf);
        candidates.swap(next_candidates);
        ++depth;
    }
}

}  // namespace heuristic

// ==================== ここから問題依存部分 ====================
// 例題: 数列を 2 組に分け、和の差の絶対値を最小化する。
// 入出力形式と実行方法は README.md を参照。

namespace problem {

using heuristic::i64;
using heuristic::u64;
using heuristic::Mwc256XXA64;

struct Input {
    vector<i64> a;
    bool read() {
        int n;
        if (!(cin >> n)) return false;
        if (n < 0 || n > 2000) return false;
        a.resize(n);
        for (auto& value : a) {
            if (!(cin >> value) || value < 0 || value > 1'000'000'000) return false;
        }
        return true;
    }
};

// 焼きなまし: 完成した解の 1 要素の所属を反転する。
struct AnnealingState {
    using Score = i64;
    struct Move { int index; };
    struct Undo { i64 sum; };
    const Input* input;
    vector<int> signs;
    i64 sum = 0;

    explicit AnnealingState(const Input& in) : input(&in) {
        for (i64 value : in.a) {
            const int sign = sum > 0 ? -1 : 1;
            signs.push_back(sign);
            sum += sign * value;
        }
    }
    Score score() const { return sum >= 0 ? -sum : sum; }
    optional<Move> propose(Mwc256XXA64& rng) const {
        if (signs.empty()) return nullopt;
        return Move{int(rng.uniform(signs.size()))};
    }
    Undo apply(const Move& move) {
        const Undo old{sum};
        sum -= 2 * signs[move.index] * input->a[move.index];
        signs[move.index] *= -1;
        return old;
    }
    void undo(const Move& move, const Undo& old) {
        signs[move.index] *= -1;
        sum = old.sum;
    }
};

// ビーム: 左から順に所属を決める。通常版・木上版で同じ状態を使える。
struct BeamState {
    using Score = i64;
    struct Move { int sign; };
    struct Undo { i64 sum; };
    const Input* input;
    vector<int> signs;
    i64 sum = 0;

    explicit BeamState(const Input& in) : input(&in) { signs.reserve(in.a.size()); }
    Score score() const { return sum >= 0 ? -sum : sum; }

    template<class Emit>
    void expand(Emit&& emit) const {
        if (signs.size() == input->a.size()) return;
        const i64 value = input->a[signs.size()];
        for (int sign : {-1, 1}) {
            const i64 next_sum = sum + sign * value;
            const Score next_score = next_sum >= 0 ? -next_sum : next_sum;
            // 同じ層では残りの入力が同じ。部分和が同じなら将来も同じなので統合可。
            // この例では部分和をそのまま 64 ビットのキーにでき、衝突もない。
            if (!emit(Move{sign}, next_score, u64(next_sum))) return;
        }
    }
    Undo apply(const Move& move) {
        const Undo old{sum};
        sum += move.sign * input->a[signs.size()];
        signs.push_back(move.sign);
        return old;
    }
    void undo(const Move&, const Undo& old) {
        signs.pop_back();
        sum = old.sum;
    }
};

}  // namespace problem

#ifndef HEURISTIC_TEMPLATE_NO_MAIN
int main() {
    enum class SearchMethod { Annealing, Beam, TreeBeam };
    constexpr SearchMethod method = SearchMethod::Annealing;
    constexpr heuristic::u64 seed = 1;

    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    const heuristic::TimeBudget budget(1.9);  // 出力用の余裕も残す
    problem::Input input;
    if (!input.read()) return 0;
    heuristic::Mwc256XXA64 rng(seed);
    vector<int> answer;

    if (method == SearchMethod::Annealing) {
        heuristic::AnnealingConfig config;
        config.start_temperature = input.a.empty() ? 1.0
            : max(1.0, double(*max_element(input.a.begin(), input.a.end())));
        config.end_temperature = 0.1;
        auto result = heuristic::simulated_annealing(
            problem::AnnealingState(input), rng, budget, config);
        answer = move(result.state.signs);
#ifdef LOCAL
        cerr << "評価値: " << result.score << " 試行回数: " << result.iterations << '\n';
#endif
    } else {
        heuristic::BeamConfig config;
        config.width = 128;
        config.max_depth = int(input.a.size());
        config.deduplicate = true;
        auto result = method == SearchMethod::Beam
            ? heuristic::beam_search(problem::BeamState(input), budget, config)
            : heuristic::tree_beam_search(problem::BeamState(input), budget, config);
        // 時間切れで部分解が返ったときも、問題に応じた貪欲法で完成させる。
        while (result.state.signs.size() < input.a.size()) {
            result.state.apply({result.state.sum > 0 ? -1 : 1});
        }
        answer = move(result.state.signs);
#ifdef LOCAL
        cerr << "完成後の評価値: " << result.state.score()
                  << " 探索深さ: " << result.depth << " 時間切れ: " << result.timed_out << '\n';
#endif
    }
    for (size_t i = 0; i < answer.size(); ++i) {
        if (i) cout << ' ';
        cout << answer[i];
    }
    cout << '\n';
}
#endif
