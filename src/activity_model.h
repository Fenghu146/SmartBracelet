#pragma once
// RF model data — auto-generated from train_rf.py
// Classes: walk(0), run(1), idle(2)

typedef struct { int f; float t; int l; int r; int cls; } RFNode;

static const RFNode tree_0[5] = {
  {8, 0.0532f, 1, 4, -1}, {9, 1.5548f, 2, 3, -1},
  {-1, 0, -1, -1, 2}, {-1, 0, -1, -1, 0}, {-1, 0, -1, -1, 1},
};
static const RFNode tree_1[11] = {
  {5, 0.1518f, 1, 4, -1}, {8, 0.0447f, 2, 3, -1},
  {-1, 0, -1, -1, 0}, {-1, 0, -1, -1, 1}, {10, 1.0125f, 5, 6, -1},
  {-1, 0, -1, -1, 2}, {9, 4.4672f, 7, 8, -1}, {-1, 0, -1, -1, 0},
  {1, -0.0063f, 9, 10, -1}, {-1, 0, -1, -1, 1}, {-1, 0, -1, -1, 1},
};
static const RFNode tree_2[7] = {
  {5, 0.4100f, 1, 6, -1}, {8, 0.0441f, 2, 5, -1},
  {6, 0.1037f, 3, 4, -1}, {-1, 0, -1, -1, 2}, {-1, 0, -1, -1, 0},
  {-1, 0, -1, -1, 1}, {-1, 0, -1, -1, 0},
};
static const RFNode tree_3[9] = {
  {5, 0.3541f, 1, 6, -1}, {7, 0.1216f, 2, 5, -1},
  {6, 0.1054f, 3, 4, -1}, {-1, 0, -1, -1, 2}, {-1, 0, -1, -1, 0},
  {-1, 0, -1, -1, 1}, {8, 0.0532f, 7, 8, -1}, {-1, 0, -1, -1, 0},
  {-1, 0, -1, -1, 1},
};
static const RFNode tree_4[5] = {
  {11, 3.2724f, 1, 4, -1}, {8, 0.0115f, 2, 3, -1},
  {-1, 0, -1, -1, 2}, {-1, 0, -1, -1, 0}, {-1, 0, -1, -1, 1},
};
static const RFNode tree_5[7] = {
  {7, 0.1518f, 1, 6, -1}, {7, 0.0317f, 2, 3, -1},
  {-1, 0, -1, -1, 2}, {11, 3.2724f, 4, 5, -1}, {-1, 0, -1, -1, 0},
  {-1, 0, -1, -1, 1}, {-1, 0, -1, -1, 1},
};
static const RFNode tree_6[5] = {
  {7, 0.0321f, 1, 2, -1}, {-1, 0, -1, -1, 2},
  {11, 3.7664f, 3, 4, -1}, {-1, 0, -1, -1, 0}, {-1, 0, -1, -1, 1},
};
static const RFNode tree_7[7] = {
  {3, 0.2873f, 1, 4, -1}, {11, 3.7664f, 2, 3, -1},
  {-1, 0, -1, -1, 0}, {-1, 0, -1, -1, 1}, {11, 1.6743f, 5, 6, -1},
  {-1, 0, -1, -1, 2}, {-1, 0, -1, -1, 1},
};
static const RFNode tree_8[5] = {
  {8, 0.0115f, 1, 2, -1}, {-1, 0, -1, -1, 2},
  {6, 0.4864f, 3, 4, -1}, {-1, 0, -1, -1, 0}, {-1, 0, -1, -1, 1},
};
static const RFNode tree_9[5] = {
  {9, 4.4628f, 1, 4, -1}, {9, 1.5829f, 2, 3, -1},
  {-1, 0, -1, -1, 2}, {-1, 0, -1, -1, 0}, {-1, 0, -1, -1, 1},
};

static const RFNode* trees[10] = { tree_0, tree_1, tree_2, tree_3, tree_4,
                                   tree_5, tree_6, tree_7, tree_8, tree_9 };

static int rf_predict(const float features[12]) {
  int votes[3] = {0};
  for (int t = 0; t < 10; t++) {
    int n = 0;
    while (1) {
      if (trees[t][n].f < 0) { votes[trees[t][n].cls]++; break; }
      if (features[trees[t][n].f] <= trees[t][n].t)
        n = trees[t][n].l;
      else
        n = trees[t][n].r;
    }
  }
  int best = 0;
  for (int c = 1; c < 3; c++) if (votes[c] > votes[best]) best = c;
  return best;
}
