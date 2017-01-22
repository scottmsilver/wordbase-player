/*
 Game Tree Search Algorithms
 Copyright (C) 2015-2016  Adam Stelmaszczyk <stelmaszczyk.adam@gmail.com>
 
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <boost/math/distributions/binomial.hpp>
#include <unordered_map>
#include <functional>
#include <sys/time.h>
#include <algorithm>
#include <iostream>
#include <assert.h>
#include <sstream>
#include <iomanip>
#include <memory>
#include <vector>
#include <random>
#include "easylogging++.h"

using namespace std;

static const int MAX_SIMULATIONS = 10000000;
static const double UCT_C = sqrt(2);
static const double WIN_SCORE = 1;
static const double DRAW_SCORE = 0.5;
static const double LOSE_SCORE = 0;

static const int MAX_DEPTH = 20;
static const int INF = 2147483647;

struct Random {
  mt19937 engine;
  
  virtual ~Random() {}
  
  int uniform(int min, int max) {
    return uniform_int_distribution<int>{min, max}(engine);
  }
};

struct Timer {
  double start_time;
  
  virtual ~Timer() {}
  
  void start() {
    start_time = get_time();
  }
  
  double get_time() const {
    timeval tv;
    gettimeofday(&tv, 0);
    return tv.tv_sec + tv.tv_usec * 1e-6;
  }
  
  double seconds_elapsed() const {
    return get_time() - start_time;
  }
  
  bool exceeded(double seconds) const {
    return seconds_elapsed() > seconds;
  }
  
  friend ostream &operator<<(ostream &os, const Timer &timer) {
    return os << setprecision(2) << fixed << timer.seconds_elapsed() << "s";
  }
};

template<class M>
struct Move {
  virtual ~Move() {}
  
  virtual void read() = 0;
  
  virtual ostream &to_stream(ostream &os) const = 0;
  
  friend ostream &operator<<(ostream &os, const Move &move) {
    return move.to_stream(os);
  }
  
  virtual bool operator==(const M &rhs) const = 0;
  
  virtual size_t hash() const = 0;
};

enum TTEntryType { EXACT_VALUE, LOWER_BOUND, UPPER_BOUND };

template<class M>
struct TTEntry {
  M move;
  int depth;
  int value;
  TTEntryType value_type;
  
  TTEntry() {}
  
  virtual ~TTEntry() {}
  
  TTEntry(const M &move, int depth, int value, TTEntryType value_type) :
  move(move), depth(depth), value(value), value_type(value_type) {}
  
  ostream &to_stream(ostream &os) {
    return os << "move: " << move << " depth: " << depth << " value: " << value << " value_type: " << value_type;
  }
  
  friend ostream &operator<<(ostream &os, const TTEntry &entry) {
    return entry.to_stream(os);
  }
};


template<class S, class M>
struct State {
  unsigned visits = 0;
  double score = 0;
  char player_to_move = 0;
  S *parent = nullptr;
  unordered_map<size_t, shared_ptr<S>> children = unordered_map<size_t, shared_ptr<S>>();
  
  State(char player_to_move) : player_to_move(player_to_move) {}
  
  virtual ~State() {}
  
  void update_stats(double result) {
    score += result;
    ++visits;
  }
  
  double get_uct(double c) const {
    assert(visits > 0);
    double parent_visits = 0.0;
    if (parent != nullptr) {
      parent_visits = parent->visits;
    }
    return (score / visits) + c * sqrt(log(parent_visits) / visits);
  }
  
  shared_ptr<S> create_child(M &move) {
    S child = clone();
    child.make_move(move);
    child.parent = (S*) this;
    return make_shared<S>(child);
  }
  
  S* add_child(M &move) {
    auto child = create_child(move);
    auto key = move.hash();
    auto pair = children.insert({key, child});
    auto it = pair.first;
    return it->second.get();
  }
  
  S* get_child(M &move) {
    auto key = move.hash();
    auto it = children.find(key);
    if (it == children.end()) {
      return nullptr;
    }
    return it->second.get();
  }
  
  virtual void swap_players() {}
  
  virtual S clone() const = 0;
  
  virtual int get_goodness() const = 0;
  
  virtual vector<M> get_legal_moves(int max_moves) const = 0;
  
  virtual char get_enemy(char player) const = 0;
  
  virtual bool is_terminal() const = 0;
  
  virtual bool is_winner(char player) const = 0;
  
  virtual void make_move(const M &move) = 0;
  
  virtual void undo_move(const M &move) = 0;
  
  virtual ostream &to_stream(ostream &os) const = 0;
  
  friend ostream &operator<<(ostream &os, const State &state) {
    return state.to_stream(os);
  }
  
  virtual bool operator==(const S &other) const = 0;
  
  virtual size_t hash() const = 0;
};

template<class S, class M>
struct Algorithm {
  Algorithm() { }
  
  Algorithm(const Algorithm& algorithm) {}
  
  virtual ~Algorithm() {}
  
  virtual void reset() {}
  
  virtual M get_move(S *state) = 0;
  
  virtual string get_name() const = 0;
  
  friend ostream &operator<<(ostream &os, const Algorithm &algorithm) {
    os << algorithm.get_name();
    return os;
  }
};

template<class S, class M>
struct Human : public Algorithm<S, M> {
  Human() : Algorithm<S, M>() {}
  
  M get_move(S *state) override {
    const vector<M> &legal_moves = state->get_legal_moves();
    if (legal_moves.empty()) {
      stringstream stream;
      state->to_stream(stream);
      throw invalid_argument("Given state is terminal:\n" + stream.str());
    }
    while (true) {
      M move = M();
      move.read();
      if (find(legal_moves.begin(), legal_moves.end(), move) != legal_moves.end()) {
        return move;
      } else {
        cout << "Move " << move << " is not legal" << endl;
      }
    }
  }
  
  string get_name() const {
    return "Human";
  }
};

template<class M>
struct MinimaxResult {
  int goodness;
  M best_move;
  bool completed;
};


class IndentingOStreambuf : public std::streambuf
{
  std::streambuf*     myDest;
  bool                myIsAtStartOfLine;
  std::string         myIndent;
  std::ostream*       myOwner;
protected:
  virtual int         overflow( int ch )
  {
    if ( myIsAtStartOfLine && ch != '\n' ) {
      myDest->sputn( myIndent.data(), myIndent.size() );
    }
    myIsAtStartOfLine = ch == '\n';
    return myDest->sputc( ch );
  }
public:
  explicit            IndentingOStreambuf(
                                          std::streambuf* dest, int indent = 4 )
  : myDest( dest )
  , myIsAtStartOfLine( true )
  , myIndent( indent, '.' )
  , myOwner( NULL )
  {
  }
  explicit            IndentingOStreambuf(
                                          std::ostream& dest, int indent = 4 )
  : myDest( dest.rdbuf() )
  , myIsAtStartOfLine( true )
  , myIndent( indent, '.' )
  , myOwner( &dest )
  {
    myOwner->rdbuf( this );
  }
  virtual             ~IndentingOStreambuf()
  {
    if ( myOwner != NULL ) {
      myOwner->rdbuf( myDest );
    }
  }
};

template<class S, class M>
struct Minimax : public Algorithm<S, M> {
  unordered_map<size_t, TTEntry<M>> transposition_table;
  double MAX_SECONDS;
  const int MAX_MOVES;
  function<int(S*)> get_goodness;
  Timer timer;
  int beta_cuts, cut_bf_sum;
  int tt_hits, tt_exacts, tt_cuts;
  int nodes, leafs;
  int mMaxDepth;
  bool mUseTranspositionTable;
  bool mUseNewIterator;
  
  Minimax(double max_seconds = 10, int max_moves = INF, function<int(S*)> get_goodness = nullptr) :
  Algorithm<S, M>(),
  transposition_table(unordered_map<size_t, TTEntry<M>>(1000000)),
  MAX_SECONDS(max_seconds),
  MAX_MOVES(max_moves),
  get_goodness(get_goodness),
  timer(Timer()),
  mMaxDepth(MAX_DEPTH), mUseTranspositionTable(false), mUseNewIterator(false) {}
  
  void reset() override {
    transposition_table.clear();
  }
  
  void setMaxSeconds(double seconds) {
    MAX_SECONDS = seconds;
  }
  
  void setMaxDepth(int depth) {
    mMaxDepth = depth;
  }
  
  void setUseTranspositionTable(bool useTranspositionTable) {
    mUseTranspositionTable = useTranspositionTable;
  }
  
  void setUseNewIterator(bool useNewIterator) {
    mUseNewIterator = useNewIterator;
  }
  
  M get_move(S *state) override {
    if (state->is_terminal()) {
      stringstream stream;
      state->to_stream(stream);
      throw invalid_argument("Given state is terminal:\n" + stream.str());
    }
    if (get_goodness == nullptr) {
      get_goodness = bind(&State<S,M>::get_goodness, state);
    }
    timer.start();
    M best_move;
    for (int max_depth = 1; max_depth <= mMaxDepth; ++max_depth) {
      LOG(DEBUG) << " { ---------------------d(" << max_depth << ")------------------------------------" << endl;
      beta_cuts = 0;
      cut_bf_sum = 0;
      tt_hits = 0;
      tt_exacts = 0;
      tt_cuts = 0;
      nodes = 0;
      leafs = 0;
      LOG(DEBUG) << *state << endl;
      
      auto result = minimax(state, max_depth, -INF, INF, 0);
      if (result.completed) {
        best_move = result.best_move;
        cout << "goodness: " << result.goodness
        << " time: " << timer
        << " move: " << best_move
        << " nodes: " << nodes
        << " leafs: " << leafs
        << " beta_cuts: " << beta_cuts
        << " cutBF: " << (double) cut_bf_sum / beta_cuts
        << " tt_hits: " << tt_hits
        << " tt_exacts: " << tt_exacts
        << " tt_cuts: " << tt_cuts
        << " tt_size: " << transposition_table.size()
        << " max_depth: " << max_depth << endl;
      }
      LOG(DEBUG) << " } ---------------------d(" << max_depth << ")------------------------------------" << endl;
      if (timer.exceeded(MAX_SECONDS)) {
        break;
      }
      cout << (double) nodes / timer.seconds_elapsed() << " nodes/s" << endl;
    }
    return best_move;
  }
  
  // Find Minimax value of the given tree,
  // Minimax value lies within a range of [alpha; beta] window.
  // Whenever alpha >= beta, further checks of children in a node can be pruned.
  MinimaxResult<M> minimax(S *state, int depth, int alpha, int beta, int indent) {
    ++nodes;
    const int alpha_original = alpha;
    
    M best_move;
    if (depth == 0 || state->is_terminal()) {
      ++leafs;
      return {get_goodness(state), best_move, false};
    }
    
    TTEntry<M> entry;
    bool entry_found = get_tt_entry(state, entry);
    if (mUseTranspositionTable && entry_found && entry.depth >= depth) {
      ++tt_hits;
      if (entry.value_type == TTEntryType::EXACT_VALUE) {
        ++tt_exacts;
        return {entry.value, entry.move, true};
      }
      if (entry.value_type == TTEntryType::LOWER_BOUND && alpha < entry.value) {
        alpha = entry.value;
      }
      if (entry.value_type == TTEntryType::UPPER_BOUND && beta > entry.value) {
        beta = entry.value;
      }
      if (alpha >= beta) {
        ++tt_cuts;
        return {entry.value, entry.move, true};
      }
    }
    
    bool generate_moves = true;
    int max_goodness = -INF;
    
    bool completed = true;
    
    if (generate_moves) {
      std::vector<M> legal_moves = state->get_legal_moves(MAX_MOVES);
      
      assert(legal_moves.size() > 0);
      for (int i = 0; i < legal_moves.size(); i++) {
        M move = legal_moves[i];
        
        S preMoveState(*state);
        state->make_move(move);
        
        const int goodness = -minimax(
                                      state,
                                      depth - 1,
                                      -beta,
                                      -alpha,
                                      indent + 1).goodness;
        LOG(DEBUG) << *state << endl;
        //state->undo_move(move); (was undo, but now is put back copy above)
        *state = preMoveState;
        
        if (timer.exceeded(MAX_SECONDS)) {
          completed = false;
          break;
        }
        if (max_goodness < goodness) {
          max_goodness = goodness;
          best_move = move;
          LOG(DEBUG) << "choosing --> h(" << goodness << ")" << best_move << endl;
          if (max_goodness >= beta) {
            ++beta_cuts;
            cut_bf_sum += i + 1;
            break;
          }
        }
        if (alpha < max_goodness) {
          alpha = max_goodness;
        }
        
      }
    }
    
    if (mUseTranspositionTable && completed) {
      update_tt(state, alpha_original, beta, max_goodness, best_move, depth);
    }
    
    return {max_goodness, best_move, completed};
  }
  
  bool get_tt_entry(S *state, TTEntry<M> &entry) {
    auto key = state->hash();
    auto it = transposition_table.find(key);
    if (it == transposition_table.end()) {
      return false;
    }
    entry = it->second;
    return true;
  }
  
  void add_tt_entry(S *state, const TTEntry<M> &entry) {
    auto key = state->hash();
    transposition_table.insert({key, entry});
  }
  
  void update_tt(S *state, int alpha, int beta, int max_goodness, M &best_move, int depth) {
    TTEntryType value_type;
    if (max_goodness <= alpha) {
      value_type = TTEntryType::UPPER_BOUND;
    }
    else if (max_goodness >= beta) {
      value_type = TTEntryType::LOWER_BOUND;
    }
    else {
      value_type = TTEntryType::EXACT_VALUE;
    }
    TTEntry<M> entry = {best_move, depth, max_goodness, value_type};
    add_tt_entry(state, entry);
  }
  
  string get_name() const override {
    return "Minimax";
  }
};

template<class S, class M>
struct MonteCarloTreeSearch : public Algorithm<S, M> {
  const double max_seconds;
  const int max_simulations;
  const bool block;
  Random random;
  
  MonteCarloTreeSearch(double max_seconds = 1,
                       int max_simulations = MAX_SIMULATIONS,
                       bool block = false) :
  Algorithm<S, M>(),
  max_seconds(max_seconds),
  block(block),
  max_simulations(max_simulations) {}
  
  M get_move(S *root) override {
    if (root->is_terminal()) {
      stringstream stream;
      root->to_stream(stream);
      throw invalid_argument("Given state is terminal:\n" + stream.str());
    }
    Timer timer;
    timer.start();
    int simulation = 0;
    while (simulation < max_simulations && !timer.exceeded(max_seconds)) {
      monte_carlo_tree_search(root);
      ++simulation;
    }
    LOG(DEBUG) << "ratio: " << root->score / root->visits << endl;
    LOG(DEBUG) << "simulations: " << simulation << endl;
    auto legal_moves = root->get_legal_moves();
    LOG(DEBUG) << "moves: " << legal_moves.size() << endl;
    for (auto move : legal_moves) {
      LOG(DEBUG) << "move: " << move;
      auto child = root->get_child(move);
      if (child != nullptr) {
        LOG(DEBUG) << " score: " << child->score
        << " visits: " << child->visits
        << " UCT: " << child->get_uct(UCT_C);
      }
      LOG(DEBUG) << endl;
    }
    return get_most_visited_move(root);
  }
  
  void monte_carlo_tree_search(S *root) {
    S *current = tree_policy(root, root);
    auto result = rollout(current, root);
    propagate_up(current, result);
  }
  
  void propagate_up(S *current, double result) {
    current->update_stats(result);
    if (current->parent) {
      propagate_up(current->parent, result);
    }
  }
  
  S* tree_policy(S *state, S *root) {
    if (state->is_terminal()) {
      return state;
    }
    M move = get_tree_policy_move(state, root);
    auto child = state->get_child(move);
    if (child == nullptr) {
      return state->add_child(move);
    }
    return tree_policy(child, root);
  }
  
  M get_most_visited_move(S *state) {
    auto legal_moves = state->get_legal_moves();
    assert(legal_moves.size() > 0);
    M best_move;
    double max_visits = -INF;
    for (auto move : legal_moves) {
      auto child = state->get_child(move);
      if (child != nullptr) {
        auto visits = child->visits;
        if (max_visits < visits) {
          max_visits = visits;
          best_move = move;
        }
      }
    }
    assert(max_visits != -INF);
    return best_move;
  }
  
  M get_best_move(S *state, S *root) {
    auto legal_moves = state->get_legal_moves();
    assert(legal_moves.size() > 0);
    M best_move;
    if (state->player_to_move == root->player_to_move) {
      // maximize
      double best_uct = -INF;
      for (auto move : legal_moves) {
        auto child = state->get_child(move);
        if (child != nullptr) {
          auto uct = child->get_uct(UCT_C);
          if (best_uct < uct) {
            best_uct = uct;
            best_move = move;
          }
        } else {
          return move;
        }
      }
    }
    else {
      // minimize
      double best_uct = INF;
      for (auto move : legal_moves) {
        auto child = state->get_child(move);
        if (child != nullptr) {
          auto uct = child->get_uct(-UCT_C);
          if (best_uct > uct) {
            best_uct = uct;
            best_move = move;
          }
        } else {
          return move;
        }
      }
    }
    return best_move;
  }
  
  M get_random_move(const S *state) {
    auto legal_moves = state->get_legal_moves();
    assert(legal_moves.size() > 0);
    int index = random.uniform(0, legal_moves.size() - 1);
    return legal_moves[index];
  }
  
  shared_ptr<M> get_winning_move(S *state) {
    auto current_player = state->player_to_move;
    auto legal_moves = state->get_legal_moves();
    assert(legal_moves.size() > 0);
    for (M &move : legal_moves) {
      state->make_move(move);
      if (state->is_winner(current_player)) {
        state->undo_move(move);
        return make_shared<M>(move);
      }
      state->undo_move(move);
    }
    return nullptr;
  }
  
  shared_ptr<M> get_blocking_move(S *state) {
    auto current_player = state->player_to_move;
    auto enemy = state->get_enemy(current_player);
    state->player_to_move = enemy;
    auto legal_moves = state->get_legal_moves();
    assert(legal_moves.size() > 0);
    for (M &move : legal_moves) {
      state->make_move(move);
      if (state->is_winner(enemy)) {
        state->undo_move(move);
        state->player_to_move = current_player;
        return make_shared<M>(move);
      }
      state->undo_move(move);
    }
    state->player_to_move = current_player;
    return nullptr;
  }
  
  M get_tree_policy_move(S *state, S *root) {
    // If player has a winning move he makes it.
    auto move_ptr = get_winning_move(state);
    if (move_ptr != nullptr) {
      return *move_ptr;
    }
    if (block) {
      // If player has a blocking move he makes it.
      move_ptr = get_blocking_move(state);
      if (move_ptr != nullptr) {
        return *move_ptr;
      }
    }
    return get_best_move(state, root);
  }
  
  M get_default_policy_move(S *state) {
    // If player has a winning move he makes it.
    auto move_ptr = get_winning_move(state);
    if (move_ptr != nullptr) {
      return *move_ptr;
    }
    // If player has a blocking move he makes it.
    move_ptr = get_blocking_move(state);
    if (move_ptr != nullptr) {
      return *move_ptr;
    }
    return get_random_move(state);
  }
  
  double rollout(S *current, S *root) {
    if (current->is_terminal()) {
      if (current->is_winner(root->player_to_move)) {
        return WIN_SCORE;
      }
      if (current->is_winner(root->get_enemy(root->player_to_move))) {
        return LOSE_SCORE;
      }
      return DRAW_SCORE;
    }
    M move = get_default_policy_move(current);
    current->make_move(move);
    auto result = rollout(current, root);
    current->undo_move(move);
    return result;
  }
  
  string get_name() const override {
    return "MonteCarloTreeSearch";
  }
  
};

template<class S, class M>
struct Tester {
  S *root = nullptr;
  Algorithm<S, M> &algorithm_1;
  Algorithm<S, M> &algorithm_2;
  const int MATCHES;
  const bool VERBOSE;
  const double SIGNIFICANCE_LEVEL = 0.005; // two sided 99% confidence interval
  
  Tester(S *state, Algorithm<S, M> &algorithm_1, Algorithm<S, M> &algorithm_2, int matches = INF, bool verbose = false) :
  root(state), algorithm_1(algorithm_1), algorithm_2(algorithm_2), MATCHES(matches), VERBOSE(verbose) {}
  
  virtual ~Tester() {}
  
  int start() {
    int draws = 0;
    int algorithm_1_wins = 0;
    int algorithm_2_wins = 0;
    char enemy = root->get_enemy(root->player_to_move);
    for (int i = 1; i <= MATCHES; ++i) {
      auto current = root->clone();
      if (i % 4 == 0 || i % 4 == 2) {
        current.player_to_move = current.get_enemy(current.player_to_move);
      }
      if (i % 4 == 0 || i % 4 == 3) {
        current.swap_players();
      }
      if (VERBOSE) {
        LOG(DEBUG) << current << endl;
      }
      while (!current.is_terminal()) {
        auto &algorithm = (current.player_to_move == root->player_to_move) ? algorithm_1 : algorithm_2;
        if (VERBOSE) {
          LOG(DEBUG) << current.player_to_move << " " << algorithm << endl;
        }
        algorithm.reset();
        Timer timer;
        timer.start();
        auto copy = current.clone();
        auto move = algorithm.get_move(&copy);
        if (VERBOSE) {
          cout << algorithm.read_log();
          cout << timer << endl;
        }
        current.make_move(move);
        if (VERBOSE) {
          cout << current << endl;
        }
      }
      cout << "Match " << i << ": ";
      if (current.is_winner(root->player_to_move)) {
        ++algorithm_1_wins;
        cout << root->player_to_move << " " << algorithm_1 << " won" << endl;
      } else if (current.is_winner(enemy)) {
        ++algorithm_2_wins;
        cout << enemy << " " << algorithm_2 << " won" << endl;
      } else {
        ++draws;
        cout << "draw" << endl;
      }
      cout << root->player_to_move << " " << algorithm_1 << " wins: " << algorithm_1_wins << endl;
      cout << enemy << " " << algorithm_2 << " wins: " << algorithm_2_wins << endl;
      cout << "Draws: " << draws << endl;
      double successes = algorithm_1_wins + 0.5 * draws;
      double ratio = successes / i;
      cout << "Ratio: " << ratio << endl;
      double lower = boost::math::binomial_distribution<>::find_lower_bound_on_p(i, successes, SIGNIFICANCE_LEVEL);
      double upper = boost::math::binomial_distribution<>::find_upper_bound_on_p(i, successes, SIGNIFICANCE_LEVEL);
      cout << "Lower confidence bound: " << lower << endl;
      cout << "Upper confidence bound: " << upper << endl;
      cout << endl;
      if (upper < 0.5 || lower > 0.5) {
        break;
      }
    }
    return draws;
  }
};