# wordbase-player
Plays the game of wordbase.

To build:

```
# Move to directory containing "src"
mkdir build
cd build
cmake ../src
make
```

To use:
```
./wordbase-driver
```
Create a new board. The preceding *, means a bomb at the letter after the *. A + means a super-bomb.
```
boardshell> nb gregmiperslmavnetlaecaosrnowykosbrilfakosalagzl*eicveonredgmdamepumselomrtleipcradsndlnoihuiai*eoisatxerhctpteroustupsyalcopaeamhves
nb gregmiperslmavnetlaecaosrnowykosbrilfakosalagzl*eicveonredgmdamepumselomrtleipcradsndlnoihuiai*eoisatxerhctpteroustupsyalcopaeamhves
```
Print the state of the board.
```
boardshell> ps
ps
player(1): h=0
   0 1 2 3 4 5 6 7 8 9
 0.G.R.E.G.M.I.P.E.R.S
 1 l m a v n e t l a e
 2 c a o s r n o w y k
 3 o s b r i l f a k o
 4 s a l a g z l*e i c
 5 v e o n r e d g m d
 6 a m e p u m s e l o
 7 m r t l e i p c r a
 8 d s n d l n o i h u
 9 i a i*e o i s a t x
10 e r h c t p t e r o
11 u s t u p s y a l c
12.o.p.a.e.a.m.h.v.e.s

```
Show me all the words at position 0, 0.
It's (row, column) starting at 0
```
boardshell> words 0 0
words 0 0
gram: (0,0),(0,1),(1,2),(1,1)
grama: (0,0),(0,1),(1,2),(1,1),(2,1)
gramas: (0,0),(0,1),(1,2),(1,1),(2,1),(3,1)
grav: (0,0),(0,1),(1,2),(1,3)
grave: (0,0),(0,1),(1,2),(1,3),(0,2)
gravs: (0,0),(0,1),(1,2),(1,3),(2,3)
graal: (0,0),(0,1),(1,2),(2,1),(1,0)
glam: (0,0),(1,0),(2,1),(1,1)
glamor: (0,0),(1,0),(2,1),(1,1),(2,2),(3,3)
glamors: (0,0),(1,0),(2,1),(1,1),(2,2),(3,3),(2,3)
glamorize: (0,0),(1,0),(2,1),(1,1),(2,2),(3,3),(3,4),(4,5),(5,5)
glamorizer: (0,0),(1,0),(2,1),(1,1),(2,2),(3,3),(3,4),(4,5),(5,5),(5,4)
glamorized: (0,0),(1,0),(2,1),(1,1),(2,2),(3,3),(3,4),(4,5),(5,5),(5,6)
glamorizes: (0,0),(1,0),(2,1),(1,1),(2,2),(3,3),(3,4),(4,5),(5,5),(6,6)
glass: (0,0),(1,0),(2,1),(3,1),(4,0)
```
Suggest a move, using 30 seconds to suggest one.
```
boardshell> sm 30
sm 30
goodness: 208 time: 0.00s move: lw(174) nodes: 306 leafs: 305 beta_cuts: 0 cutBF: nan tt_hits: 0 tt_exacts: 0 tt_cuts: 0 tt_size: 1 max_depth: 1
444102.78 nodes/s
goodness: -58 time: 0.01s move: lw(174) nodes: 1194 leafs: 888 beta_cuts: 140 cutBF: 1.00 tt_hits: 163 tt_exacts: 13 tt_cuts: 150 tt_size: 143 max_depth: 2
226647.31 nodes/s
goodness: 772 time: 0.23s move: lw(265) nodes: 75196 leafs: 73070 beta_cuts: 1203 cutBF: 1.40 tt_hits: 796 tt_exacts: 14 tt_cuts: 756 tt_size: 1167 max_depth: 3
325467.45 nodes/s
goodness: -261 time: 6.03s move: lw(13) nodes: 237550 leafs: 94573 beta_cuts: 55128 cutBF: 1.07 tt_hits: 123270 tt_exacts: 1079 tt_cuts: 86388 tt_size: 19825 max_depth: 4
39365.64 nodes/s
suggested move: glamorizes:(0,0),(1,0),(2,1),(1,1),(2,2),(3,3),(3,4),(4,5),(5,5),(6,6)
lw(13)
player(2): h=-168
   0 1 2 3 4 5 6 7 8 9
 0.G.R.E.G.M.I.P.E.R.S
 1.L.M a v n e t l a e
 2 c.A.O s r n o w y k
 3 o s b.R.I l f a k o
 4 s a l a g.Z l*e i c
 5 v e o n r.E d g m d
 6 a m e p u m.S e l o
 7 m r t l e i p c r a
 8 d s n d l n o i h u
 9 i a i*e o i s a t x
10 e r h c t p t e r o
11 u s t u p s y a l c
12.o.p.a.e.a.m.h.v.e.s
```
Now show me where that word appears - legal word move (lwm).
```
boardshell> lwm glamorizes
lwm glamorizes
glamorizes: (0,0),(1,0),(2,1),(1,1),(2,2),(3,3),(3,4),(4,5),(5,5),(6,6): h=-168
```
Now make the move.
```
boardshell> m (0,0),(1,0),(2,1),(1,1),(2,2),(3,3),(3,4),(4,5),(5,5),(6,6)
m (0,0),(1,0),(2,1),(1,1),(2,2),(3,3),(3,4),(4,5),(5,5),(6,6)
making move: "glamorizes": lw(13)
```
Show me the state again.
```
boardshell> ps
ps
player(2): h=-168
   0 1 2 3 4 5 6 7 8 9
 0.G.R.E.G.M.I.P.E.R.S
 1.L.M a v n e t l a e
 2 c.A.O s r n o w y k
 3 o s b.R.I l f a k o
 4 s a l a g.Z l*e i c
 5 v e o n r.E d g m d
 6 a m e p u m.S e l o
 7 m r t l e i p c r a
 8 d s n d l n o i h u
 9 i a i*e o i s a t x
10 e r h c t p t e r o
11 u s t u p s y a l c
12.o.p.a.e.a.m.h.v.e.s

```
