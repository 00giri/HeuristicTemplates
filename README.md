# ヒューリスティックコンテスト用テンプレート

[main.cpp](main.cpp) は、焼きなまし法・ビームサーチ・木上のビームサーチを使うための単一ファイルのテンプレートです。問題に依存する入力・状態・遷移・出力を末尾にまとめています。

C++17 以上、`bits/stdc++.h` と 128 ビット整数拡張を利用できる環境が必要です。GCC、または libstdc++ を利用する Clang を想定しています。

## ビルドと実行

使う探索は `main.cpp` の `main()` 冒頭で設定します。`method` を次のいずれかに変更し、再コンパイルしてください。

```cpp
enum class SearchMethod { Annealing, Beam, TreeBeam };
constexpr SearchMethod method = SearchMethod::Annealing;
constexpr heuristic::u64 seed = 1;
```

| `method` に設定する値 | 探索方法 |
| --- | --- |
| `SearchMethod::Annealing` | 焼きなまし法（初期設定） |
| `SearchMethod::Beam` | 通常のビームサーチ |
| `SearchMethod::TreeBeam` | 木上のビームサーチ |

乱数の seed も同じ場所の `seed` で設定します。初期値は `1` です。付属例のビームサーチは乱数を使いません。

```bash
g++ -std=c++17 -O2 -DNDEBUG -Wall -Wextra main.cpp -o main
```

付属の例題は、数列を 2 組に分け、両者の和の差の絶対値を最小化する問題です。入力は要素数 `N` と `N` 個の整数 `a[i]` です。範囲は `0 <= N <= 2000`、`0 <= a[i] <= 10^9` としています。

```text
6
2 3 5 7 11 13
```

この入力を `input.txt` に保存し、実行します。

```bash
./main < input.txt
```

出力は、各要素の所属を表す `-1` または `1` を `N` 個並べたものです。標準出力には解だけを出します。コンパイル時に `-DLOCAL` を付けると、標準エラー出力に評価値や探索回数などを表示します。

## 問題に合わせて書き換える箇所

1. `problem::Input` の入力形式を変更します。
2. `problem::AnnealingState` または `problem::BeamState` の状態・評価値・遷移を変更します。
3. `main()` で探索方法、乱数の seed、制限時間、温度、ビーム幅などを設定します。
4. 最終状態から解を出力します。ビームが部分解を返す場合は、完成させる処理も用意します。

先頭には `using namespace std;`、`ll`、`rep(i, s, n)`、`all(v)` を定義しています。例えば、問題依存部分では次のように書けます。

```cpp
vector<ll> a(n);
rep(i, 0, n) cin >> a[i];
sort(all(a));
```

探索部品は `heuristic` 名前空間にあります。評価値はすべて**大きいほど良い**として扱います。最小化問題では、コストの符号を反転するなどして変換してください。`Score` には `ll`、`int64_t`、`double` などを使えます。NaN・無限大は使わないでください。

| 探索方法 | 向いている場面 | 状態の扱い |
| --- | --- | --- |
| `simulated_annealing` | 完成した解を少しずつ変更する | 遷移を適用し、棄却時に巻き戻す。最良解の更新時にコピーする |
| `beam_search` | 状態が小さく、コピーが軽い | 採用する候補についてだけ親状態をコピーする |
| `tree_beam_search` | 状態が大きく、差分更新が軽い | 1 個の状態を木に沿って適用・巻き戻ししながら使う |

## 状態のインターフェース

| 型・関数 | 必要な探索 | 役割 |
| --- | --- | --- |
| `State::Score` | すべて | 評価値の型 |
| `State::Move` | すべて | 1 回の操作を表す型 |
| `score() const` | すべて | 現在の評価値を返す |
| `apply(const Move&)` | すべて | 操作を適用する。焼きなまし法・木上版では巻き戻し情報を返す |
| `State::Undo` | 木上版 | `apply` が返す巻き戻し情報の型。焼きなまし法の例でもこの名前を使う |
| `undo(const Move&, const Undo&)` | 焼きなまし法・木上版 | 操作直前の状態へ戻す |
| `propose(Mwc256XXA64&) const` | 焼きなまし法 | `optional<Move>` として近傍を 1 つ提案する |
| `expand(Emit&& emit)` | 通常版・木上版 | 合法手と遷移後の評価値を列挙する |

`Move` と `Undo` は小さい値型にし、状態内部への参照を持たせないでください。`score()` はキャッシュした評価値を返し、`apply` と `undo` で差分更新すると高速です。巻き戻す際は、評価値やハッシュを含むすべての状態を元に戻してください。

焼きなまし法と通常版では状態をコピーできる必要があります。木上版は状態を移動できれば使えます。入力を状態からポインタで参照する場合は、探索結果を利用し終えるまで入力を生存させてください。

### ビームの候補列挙

状態に次の形のメンバ関数を実装します。

```cpp
template<class Emit>
void expand(Emit&& emit) const {
    // 合法手を列挙し、各手について次を呼ぶ。
    // if (!emit(move, next_score, next_hash)) return;
}
```

`next_score` は遷移後の状態の評価値です。`emit` が `false` を返したら時間切れなので、直ちに列挙を終了してください。重複除去を使わない場合は `emit(move, next_score)` と書けます。

差分評価を直接計算する代わりに、一時的に `apply` して評価し、`undo` してから `emit` しても構いません。この場合は `expand` の `const` を外せます。どの終了経路でも、呼び出し前の状態へ戻す必要があります。

## 焼きなまし法

以下は、付属例の `input` を読み込んだ後に呼び出す例です。

```cpp
heuristic::TimeBudget budget(1.9);
heuristic::Mwc256XXA64 rng(42);
heuristic::AnnealingConfig config;
config.start_temperature = 100.0;
config.end_temperature = 0.1;

auto result = heuristic::simulated_annealing(
    problem::AnnealingState(input), rng, budget, config);
// result.state から解を出力する。
```

| 設定 | 初期値 | 意味 |
| --- | --- | --- |
| `start_temperature` | `100.0` | 開始温度。正の有限値 |
| `end_temperature` | `1.0` | 終了温度。正の有限値 |
| `max_iterations` | `uint64_t` の最大値 | 最大試行回数。初期値は実質的に回数制限なし |
| `update_interval` | `64` | 時計と温度を更新する試行間隔。1 以上 |

温度は幾何冷却で更新します。改善・同点の遷移を受理し、悪化する遷移は `exp(評価値の差 / 温度)` の確率で受理します。温度は、その問題の評価値の変化量に合わせて調整してください。

`propose` の `nullopt` は「近傍がなく探索終了」を意味します。無効な近傍は `propose` 内で除外してください。時間制限と回数制限を併用した場合は、先に達した制限で終了します。

結果の `state` と `score` は探索中の最良解です。`iterations` は試行回数、`accepted` は受理した回数です。

## ビームサーチ・木上のビームサーチ

```cpp
heuristic::TimeBudget budget(1.9);
heuristic::BeamConfig config;
config.width = 128;
config.max_depth = int(input.a.size());
config.deduplicate = true;

auto result = heuristic::beam_search(
    problem::BeamState(input), budget, config);
// 木上版へ切り替える場合は、関数名を tree_beam_search に変更する。
```

| 設定 | 初期値 | 意味 |
| --- | --- | --- |
| `width` | `100` | 各層に残す状態数の上限。1 以上 |
| `max_depth` | `100` | 最大手数。0 以上 |
| `deduplicate` | `false` | 同じ層の同一ハッシュを統合するか |
| `time_check_interval` | `256` | 候補列挙・展開開始時などに時計を確認する呼び出し間隔。1 以上 |

どちらも固定手数型で、最後に完成した層の最良状態を返します。途中層との評価値の比較は行いません。途中で終わる解を後の層にも残したい場合は、終了状態から状態不変の待機手を生成してください。

| 結果 | 意味 |
| --- | --- |
| `state` | 最後に完成した層の最良状態 |
| `score` | その状態の評価値 |
| `moves` | 渡した初期状態からの操作列 |
| `depth` | 完成した層の深さ。`moves.size()` と一致 |
| `timed_out` | 時間制限によって探索を打ち切ったか |

時間切れや全候補消滅では `depth < max_depth` になることがあります。完全な解が必要な問題では、結果を貪欲法などで完成させてください。付属例の `main()` に補完処理があります。

重複除去を有効にする場合は、将来の遷移・評価に関係する情報をすべてハッシュに含めてください。同じハッシュを同じ状態とみなすため、ハッシュ衝突で候補を失う可能性があります。付属例では、同じ層の同じ部分和を統合しています。

## 乱数生成器

```cpp
heuristic::Mwc256XXA64 rng(42);
auto value = rng.next_u64();  // 64 ビットの乱数。rng() でも同じ
auto index = rng.fast(100);  // [0, 100) の高速版
auto exact = rng.uniform(100);  // [0, 100) の偏りを除く版
double probability = rng.uniform01();  // [0, 1)
rng.seed(42);  // 初期状態へ戻す
shuffle(all(a), rng);  // 標準ライブラリにも渡せる
```

`fast(n)` と `uniform(n)` は `n > 0` が前提です。

| 関数 | 変換方法・使い分け |
| --- | --- |
| `fast(n)` | 乗算の上位ビットを使い、除算・再抽選を行わない。各出力値に対応する元の乱数の個数に最大 1 個の差があるため、小さな `n` 向け。`n` が 2 の冪なら偏りなし |
| `uniform(n)` | Lemire 法で変換による偏りを除く。小さな `n` では、ほぼ常に除算・再抽選が不要 |
| `uniform01()` | 上位 53 ビットを使う。`0.0` を含み、`1.0` は含まない |

2 つのキーで初期化する `Mwc256XXA64(key1, key2)` と `seed(key1, key2)` も利用できます。

乱数生成の参照先は [Mwc256XXA64 の原実装](https://github.com/tkaitchuck/Mwc256XXA64/blob/main/impl/src/gen64.rs)、範囲変換の参照先は [Lemire 法の論文](https://arxiv.org/abs/1805.10941)です。

## 時間管理と再現性

`TimeBudget` の単位は秒です。計時は構築時に始まるため、入力や前処理も制限時間に含める場合は、それらの前に構築します。複数の探索に同じオブジェクトを渡すと、共通の締切になります。

引数なしの `TimeBudget` は時間無制限です。固定 seed と固定試行回数・深さを組み合わせると、実行速度の違いによる結果の変動を避けられます。時間無制限で焼きなまし法を使う場合は、必ず `max_iterations` を設定してください。

```cpp
heuristic::TimeBudget unlimited;
heuristic::Mwc256XXA64 rng(42);
heuristic::AnnealingConfig config;
config.max_iterations = 100000;

auto result = heuristic::simulated_annealing(
    problem::AnnealingState(input), rng, unlimited, config);
```

時間制限は協調的です。時計確認の間隔、1 回の遷移、候補選抜、木上版の 1 回の葉間移動、最後の経路復元・出力にかかる時間だけ超過しうるため、提出時には余裕を取ってください。

## 速度とメモリの調整

通常版は採用する候補だけ状態をコピーします。木上版は [「木上のビームサーチ：高速化編」](https://trap.jp/post/2920/) を参考に、帰りがけ順の連続配列で経路を共有します。状態を最後に訪問した葉に保持し、次の葉との最近共通祖先までだけ巻き戻すため、各層で根へ戻る必要がありません。共通の経路は動かさず、必要な枝だけを次の配列に詰め直します。

ビーム幅が 1 の場合は、候補の最大値だけを保持して前進します。葉間の移動と巻き戻し情報の保存を省きます。

| 項目 | おおよそのコスト |
| --- | --- |
| 重複除去なしの候補選抜 | 平均 `O(候補数 + 幅 log 幅)` |
| 重複除去ありの候補選抜 | `O(候補数 log 候補数)` |
| 通常版の状態・履歴 | `O(幅 × State の大きさ + 幅 × 深さ × Move の大きさ)` |
| 木上版の状態・経路配列・巻き戻し情報 | 最悪 `O(State の大きさ + 幅 × 深さ × Move の大きさ + 深さ × Undo の大きさ)` |
| 両ビームの一時候補 | `O(幅 × 分岐数)` 個 |

木上版の経路の移動・コピーは、保持する経路の辺数に比例する処理です。状態が小さい場合や、深い枝が多く残る場合は通常版が速いこともあります。実際の問題で計測し、ビーム幅と時計確認間隔を調整してください。

付属例のビーム評価は部分和の差だけを使っています。問題に合わせて、将来の見込みも評価に含めると探索を改善できます。

### 木上版の速度比較

[benchmarks/tree_beam_benchmark.cpp](benchmarks/tree_beam_benchmark.cpp) で、変更前の親・兄弟リンクによる木の管理と、現在の帰りがけ順の配列による管理を比較できます。変更前の実装は [benchmarks/legacy_tree.hpp](benchmarks/legacy_tree.hpp) に保存しています。

記事は Rust のテンプレートを提示しており、問題に依存する遷移・評価・候補選抜を別途実装する必要があります。ここでの測定は、その経路管理方式を本テンプレートのインターフェースに合わせて実装した C++ 版と旧版の比較です。記事掲載の Rust 版そのものや、筆者のコンテスト提出コードとの直接比較ではありません。

```bash
g++ -std=c++17 -O2 -DNDEBUG -Wall -Wextra benchmarks/tree_beam_benchmark.cpp -o /tmp/tree_beam_benchmark
/tmp/tree_beam_benchmark 9
```

測定環境は AMD Ryzen 5 3500U、GCC 14.3.0、`-O2 -DNDEBUG` です。固定深さ・時間無制限で、同じ状態・評価関数・候補選抜を使います。初回のウォームアップを除き、実行順を交替させた 9 回の中央値を採用しました。候補生成・選抜・最終経路復元を含め、初期状態の構築と結果検査は計測から除いています。

| ケース | 幅 | 深さ | 旧版 | 現在版 | 高速化倍率 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 盤面 128 B・分岐数 8 | 128 | 400 | 30.836 ms | 23.085 ms | 1.336 倍 |
| 盤面 64 KiB・分岐数 8 | 128 | 400 | 30.381 ms | 26.311 ms | 1.155 倍 |
| 盤面 64 KiB・分岐数 16 | 512 | 240 | 116.566 ms | 97.262 ms | 1.198 倍 |
| 根から分かれた 8 本の長い枝 | 8 | 1800 | 357.256 ms | 78.160 ms | 4.571 倍 |
| 一本道 | 1 | 50000 | 5.232 ms | 1.974 ms | 2.651 倍 |
| 数列の分割・重複除去あり | 128 | 600 | 658.817 ms | 96.876 ms | 6.801 倍 |

各試行で最終評価値・解・経路の復元結果を照合しています。最初の 5 ケースは合成問題、最後は付属例題です。これらの条件では旧版の約 1.15〜6.80 倍の速度になりました。任意の問題で同じ倍率になる保証はなく、状態更新や候補選抜の重さによって効果は変わります。

実測値は [benchmarks/tree_beam_results.csv](benchmarks/tree_beam_results.csv) にも保存しています。CSV の適用・巻き戻し回数は計時とは別に数えた値です。数列の分割では回数を計測していないため、その欄は `0` としています。

## テスト

リポジトリのルートで実行します。テストは `assert` を使うため、`-DNDEBUG` を付けないでください。

```bash
g++ -std=c++17 -O2 -Wall -Wextra tests/template_test.cpp -o /tmp/template_test
/tmp/template_test
```

メモリ破壊や未定義動作を検査する場合は、次のようにビルドします。

```bash
g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/template_test.cpp -o /tmp/template_test_san
/tmp/template_test_san
```

[tests/template_test.cpp](tests/template_test.cpp) では、既知の乱数列、乱数の範囲、焼きなまし法の受理・棄却と最良解保持、全探索・独立した参照実装との比較、深い木、経路復元、時間切れ時の巻き戻しを検証します。木上版では最良候補を訪問する前後を含む 1080 通りの中断条件も検査します。テストから利用するため、`HEURISTIC_TEMPLATE_NO_MAIN` を定義すると `main()` だけを除外できます。
