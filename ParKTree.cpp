#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <vector>
#include <utility>
#include <algorithm>
#include <string>
#include <random>
#include <unordered_set>
#include <unordered_map>
#include <set>
#include <omp.h>

using namespace std;

/** Char to binary encoding */
const vector<uint8_t> nucleotideIndex{ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,3 };
const vector<char> signatureIndex{ 'A', 'C', 'G', 'T' };

static float density;         // % of sequence set as bits
static bool fastaOutput;      // Output fasta or csv

vector<pair<string, string>> loadFasta(const char *path)
{
  vector<pair<string, string>> sequences;

  FILE *fp = fopen(path, "r");
  if (!fp) {
    fprintf(stderr, "Failed to load %s\n", path);
    exit(1);
  }
  for (;;) {
    char seqNameBuf[8192];
    if (fscanf(fp, " >%[^\n]\n", seqNameBuf) < 1) break;
    string sequenceBuf;

    for (;;) {
      int c = fgetc(fp);
      if (c == EOF || c == '>') {
        ungetc(c, fp);
        break;
      }
      if (isalpha(c)) {
        sequenceBuf.push_back(c);
      }
    }
    sequences.push_back(make_pair(string(seqNameBuf), sequenceBuf));
  }
  fclose(fp);

  return sequences;
}

void generateSignature(uint64_t *output, const pair<string, string> &fasta)
{
  // Binary encode genetic string
  string fastaSequence = fasta.second;
  uint64_t sig = 0;
  for (size_t j = 0; j < fastaSequence.length(); j++) {
    char c = fastaSequence[j];
    sig |= (uint64_t)(nucleotideIndex[c]) << (j * 2);
  }
  *output = sig;
}

vector<uint64_t> convertFastaToSignatures(const vector<pair<string, string>> &fasta)
{
  vector<uint64_t> output;
  // Allocate space for the strings
  output.resize(fasta.size());
  // output.resize(fasta.size() * signatureSize);
  
  #pragma omp parallel for schedule(dynamic)
  for (size_t i = 0; i < fasta.size(); i++) {
    generateSignature(&output[i], fasta[i]);
    // generateSignature(&output[signatureSize * i], fasta[i]);
  }
  
  return output;
}

void outputClusters(const vector<size_t> &clusters)
{
  for (size_t sig = 0; sig < clusters.size(); sig++)
  {
    printf("%llu,%llu\n", static_cast<unsigned long long>(sig), static_cast<unsigned long long>(clusters[sig]));
  }
}

void outputFastaClusters(const vector<size_t> &clusters, const vector<pair<string, string>> &fasta)
{
  fprintf(stderr, "Writing out %zu records\n", clusters.size());
  for (size_t sig = 0; sig < clusters.size(); sig++)
  {
    printf(">%llu\n%s\n", static_cast<unsigned long long>(clusters[sig]), fasta[sig].second.c_str());
  }
}
/*
vector<size_t> clusterSignatures(const vector<uint64_t> &sigs)
{
  auto rng = ranlux24_base();
  
  auto dist = uniform_int_distribution<size_t>(0, clusterCount - 1);
  size_t sigCount = sigs.size() / signatureSize;
  vector<size_t> clusters(sigCount);
  
  for (size_t i = 0; i < sigCount; i++) {
    clusters[i] = dist(rng);
  }
  
  return clusters;
}
*/

// Parameters
size_t ktree_order = 10;
size_t ktree_capacity = 1000000;

// void dbgPrintSignature(const uint64_t *sig)
// {
//   fprintf(stderr, "%p: ", sig);
//   for (size_t i = 0; i < 64; i++) {
//   // for (size_t i = 0; i < signatureSize * 64; i++) {
//     if (sig[i / 64] & (1ull << (i % 64))) {
//       fprintf(stderr, "1");
//     } else {
//       fprintf(stderr, "0");
//     }
//   }
//   fprintf(stderr, "\n");
// }

// Convert binary signature back to genetic string
void dbgPrintSignature(const uint64_t *sig)
{
  fprintf(stderr, "%p: ", sig);
  uint64_t signature  = *sig;
  string sequence = string(20, ' ');
  for (size_t j = 0; j < 20; j++) {
    sequence[j] = signatureIndex[(signature >> (j * 2)) & 0x3];
  }
  fprintf(stderr, "%s", sequence.c_str());
  fprintf(stderr, "\n");
}

void dbgPrintMatrix(const uint64_t *matrix)
{
  size_t ktree_csig_height = (ktree_order + 63) / 64;
  for (size_t i = 0; i < 64; i++) {
  // for (size_t i = 0; i < signatureSize * 64; i++) {
    fprintf(stderr, "%03zu:", i);
    for (size_t j = 0; j < ktree_csig_height * 64; j++) {
      auto val = matrix[i * ktree_csig_height + (j / 64)];
      if (val & (1ull << (j % 64))) {
        fprintf(stderr, "1");
      } else {
        fprintf(stderr, "0");
      }
    }
    fprintf(stderr, "\n");
    if (i >= 5) {
      fprintf(stderr, "...............\n");
      break;
    }
  }
}

template<class RNG>
vector<uint64_t> createRandomSigs(RNG &&rng, const vector<uint64_t> &sigs)
{
  constexpr size_t clusterCount = 2;
  vector<uint64_t> clusterSigs(clusterCount);
  size_t signatureCount = sigs.size();
  uniform_int_distribution<size_t> dist(0, signatureCount - 1);
  bool finished = false;
  
  unordered_set<string> uniqueSigs;
  for (size_t i = 0; i < signatureCount; i++) {
    size_t sig = dist(rng);
    string sigData(sizeof(uint64_t), ' ');
    memcpy(&sigData[0], &sigs[sig], sizeof(uint64_t));
    uniqueSigs.insert(sigData);
    if (uniqueSigs.size() >= clusterCount) {
      finished = true;
      break;
    }
  }
  
  size_t i = 0;
  for (const auto &sig : uniqueSigs) {
    memcpy(&clusterSigs[i], sig.data(), sizeof(uint64_t));
    i++;
  }
  
  if (!finished) {
    if (uniqueSigs.size() != 1) {
      fprintf(stderr, "This should not happen\n");
      exit(1);
    }
    clusterSigs.push_back(clusterSigs[0]);
  }
  
  return clusterSigs;
}

vector<vector<size_t>> createClusterLists(const vector<size_t> &clusters)
{
  constexpr size_t clusterCount = 2;
  vector<vector<size_t>> clusterLists(clusterCount);
  for (size_t i = 0; i < clusters.size(); i++) {
    clusterLists[clusters[i]].push_back(i);
  }
  return clusterLists;
}

vector<uint64_t> createClusterSigs(const vector<vector<size_t>> &clusterLists, const vector<uint64_t> &sigs)
{
  constexpr size_t clusterCount = 2;
  vector<uint64_t> clusterSigs(clusterCount);

  for (size_t cluster = 0; cluster < clusterLists.size(); cluster++) {
    size_t minAvgDist = numeric_limits<size_t>::max();
    // Compare all of the sigs in the cluster against each other and find sig with lowest avg dist
    for (size_t outterCount : clusterLists[cluster]) {
      const uint64_t *sigToCalc = &sigs[outterCount];
      double averageDist = 0;
      for (size_t inneCount : clusterLists[cluster]) {
        const uint64_t *sigInCluster = &sigs[outterCount];
        // Don't include self
        if (sigToCalc == sigInCluster) {
          continue;
        }
        uint64_t xoredSignatures = *sigToCalc ^ *sigInCluster;
        uint64_t evenBits = xoredSignatures & 0xAAAAAAAAAAAAAAAAULL;
        uint64_t oddBits = xoredSignatures & 0x5555555555555555ULL;
        uint64_t mismatches = (evenBits >> 1) | oddBits;
        averageDist += __builtin_popcountll(mismatches);
      }
      averageDist /= clusterLists[cluster].size() - 1;
      if (averageDist < minAvgDist) {
        minAvgDist = averageDist;
        clusterSigs[cluster] = *sigToCalc;
      }
    }  
  }
  return clusterSigs;
}

void reclusterSignatures(vector<size_t> &clusters, const vector<uint64_t> &meanSigs, const vector<uint64_t> &sigs)
{
  set<size_t> allClusters;
  for (size_t sig = 0; sig < clusters.size(); sig++) {
    const uint64_t *sourceSignature = &sigs[sig];
    size_t minHdCluster = 0;
    size_t minHd = numeric_limits<size_t>::max();

    for (size_t cluster = 0; cluster < 2; cluster++) {
      const uint64_t *clusterSignature = &meanSigs[cluster];
      uint64_t xoredSignatures = *sourceSignature ^ *clusterSignature;
      uint64_t evenBits = xoredSignatures & 0xAAAAAAAAAAAAAAAAULL;
      uint64_t oddBits = xoredSignatures & 0x5555555555555555ULL;
      uint64_t mismatches = (evenBits >> 1) | oddBits;
      size_t hd = __builtin_popcountll(mismatches);
      if (hd < minHd) {
        minHd = hd;
        minHdCluster = cluster;
      }
    }
    clusters[sig] = minHdCluster;
    allClusters.insert(minHdCluster);
  }
  
  if (allClusters.size() == 1) {
    // We can't have everything in the same cluster.
    // If this did happen, just split them evenly
    for (size_t sig = 0; sig < clusters.size(); sig++) {
      clusters[sig] = sig % 2;
    }
  }
}

// There are two kinds of ktree nodes- branch nodes and leaf nodes
// Both contain a signature matrix, plus their own signature
// (the root node signature does not matter and can be blank)
// Branch nodes then contain 'order' links to other nodes
// Leaf nodes do not.
// However, as leaf nodes may become branch nodes, we allocate
// the space anyway.
// As the space to be used is determined at runtime, we use
// parallel arrays, not structs

struct KTree {
  size_t root = numeric_limits<size_t>::max(); // # of root node
  vector<size_t> childCounts; // n entries, number of children
  vector<int> isBranchNode; // n entries, is this a branch node
  vector<size_t> childLinks; // n * o entries, links to children
  vector<size_t> parentLinks; // n entries, links to parents
  vector<uint64_t> means; // n * signatureSize entries, node signatures
  vector<uint64_t> matrices; // n * (o / 64) * signatureSize * 64 entries
  vector<omp_lock_t> locks; // n locks
  size_t order;
  size_t capacity = 0; // Set during construction, currently can't change
  size_t matrixHeight;
  size_t matrixSize;
  
  void reserve(size_t capacity) {
    // For safety, only call this at startup currently
    if (this->capacity != 0) {
      fprintf(stderr, "Reserve can only be called from 0 capacity\n");
      exit(1);
    }
    this->capacity = capacity;
    matrixHeight = (order + 63) / 64;
    matrixSize = matrixHeight * 64;
    
    #pragma omp parallel
    {
      #pragma omp single
      {
        childCounts.resize(capacity);
      }
      #pragma omp single
      {
        isBranchNode.resize(capacity);
      }
      #pragma omp single
      {
        childLinks.resize(capacity * order);
      }
      #pragma omp single
      {
        parentLinks.resize(capacity);
      }
      #pragma omp single
      {
        locks.resize(capacity);
      }
      #pragma omp single
      {
        matrices.resize(capacity * matrixSize);
      }
      #pragma omp single
      {
        means.resize(capacity);
      }
    }
  }
  
  KTree(size_t order_, size_t capacity) : order{order_} {    
    reserve(capacity);
  }
  
  size_t calcDist(const uint64_t *a, const uint64_t *b) const
  {
    uint64_t xoredSignatures = *a ^ *b;
    uint64_t evenBits = xoredSignatures & 0xAAAAAAAAAAAAAAAAULL;
    uint64_t oddBits = xoredSignatures & 0x5555555555555555ULL;
    uint64_t mismatches = (evenBits >> 1) | oddBits;
    return __builtin_popcountll(mismatches);
  }
  
  // Find where in the tree to insert the PARAM signature by traversing the tree.
  size_t traverse(const uint64_t *signature) const
  {
    size_t node = root;
    while (isBranchNode[node]) {
      size_t lowestDist = numeric_limits<size_t>::max();
      size_t lowestDistChild = 0;
      
      for (size_t i = 0; i < childCounts[node]; i++) {
        size_t child = childLinks[node * order + i];
        size_t dist = calcDist(&means[child], signature);
        if (dist < lowestDist) {
          lowestDist = dist;
          lowestDistChild = child;
        }
      }
      node = lowestDistChild;
    }
    return node;
  }
  
  // A method of store all signatures in a matrix. Can be used to extact all signautes in this node
  void addSigToMatrix(uint64_t *matrix, size_t child, const uint64_t *sig) const
  {
    size_t childPos = child / 64;
    size_t childOff = child % 64;
    
    //fprintf(stderr, "Adding this signature:\n");
    //dbgPrintSignature(sig);
    //fprintf(stderr, "To this matrix:\n");
    //dbgPrintMatrix(matrix);
    
    for (size_t i = 0; i < 64; i++) {
    //for (size_t i = 0; i < signatureSize * 64; i++) {
      matrix[i * matrixHeight + childPos] |= ((sig[i / 64] >> (i % 64)) & 0x01) << childOff;
    }
    //fprintf(stderr, "Resulting in:\n");
    //dbgPrintMatrix(matrix);
  }

  void removeSigFromMatrix(uint64_t *matrix, size_t child) const
  {
    size_t childPos = child / 64;
    size_t childOff = child % 64;
    
    uint64_t mask = ~(1ull << childOff);
    
    //fprintf(stderr, "Removing the %zuth child from matrix\n", child);    
    for (size_t i = 0; i < 64; i++) {
      matrix[i * matrixHeight + childPos] &= mask;
    }
    //fprintf(stderr, "Resulting in:\n");
    //dbgPrintMatrix(matrix);
  }
  
  void recalculateSig(size_t node)
  {
    size_t nodeSigCount = childCounts[node];
    vector<uint64_t> sigs(nodeSigCount);
    for (int i = 0; i < childCounts[node]; i++) {
      uint64_t *currentSig = &sigs[i];
      uint64_t *matrix = &matrices[node * matrixSize];
      for (size_t j = 0; j < 64; j++) {
        currentSig[j / 64] |= ((matrix[j * matrixHeight + i / 64] >> (i % 64)) & 1) << (j % 64);
      }
    }

    size_t minAvgDist = numeric_limits<size_t>::max();
    uint64_t *meanSig = &means[node];
    // Compare all of the sigs in the cluster against each other and find sig with lowest avg dist
    for (const uint64_t& sigToCalc : sigs) {
      double averageDist = 0;
      for (const uint64_t& sigInCluster : sigs) {
        // Don't include self
        if (sigToCalc == sigInCluster) {
          continue;
        }
        uint64_t xoredSignatures = sigToCalc ^ sigInCluster;
        uint64_t evenBits = xoredSignatures & 0xAAAAAAAAAAAAAAAAULL;
        uint64_t oddBits = xoredSignatures & 0x5555555555555555ULL;
        uint64_t mismatches = (evenBits >> 1) | oddBits;
        averageDist += __builtin_popcountll(mismatches);
      }
      averageDist /= nodeSigCount - 1;
      if (averageDist < minAvgDist) {
        minAvgDist = averageDist;
        *meanSig = sigToCalc;
      }
    }
  }

  // From node, recalculate node middle points
  void recalculateUp(size_t node)
  {
    size_t limit = 10;
    //fprintf(stderr, "RecalculateUp %zu\n", node);
    while (node != root) {
      recalculateSig(node);
      node = parentLinks[node];
      if (omp_test_lock(&locks[node])) {
        omp_unset_lock(&locks[node]);
      } else {
        break;
      }
      
      // Put a limit on how far we go up
      // At some point it stops mattering, plus this helps avoid inf loops
      // caused by cycles getting into the tree structure
      limit--;
      if (limit == 0) return;
      //fprintf(stderr, "-> %zu\n", node);
    }
  }
  
  size_t getNewNodeIdx(vector<size_t> &insertionList)
  {
    if (insertionList.empty()) {
      fprintf(stderr, "ERROR: ran out of insertion points\n");
      exit(1);
    }
    size_t idx = insertionList.back();
    insertionList.pop_back();
    
    // Initialise lock
    omp_init_lock(&locks[idx]);
    return idx;
  }
  
  template<class RNG>
  void splitNode(RNG &&rng, size_t node, const uint64_t *sig, vector<size_t> &insertionList, size_t link)
  {
    //fprintf(stderr, "Splitting node %zu\n", node);
    // Add 'sig' to the current node, splitting it in the process
    //fprintf(stderr, "Adding signature:\n");
    //dbgPrintSignature(sig);
    size_t nodeSigs = childCounts[node] + 1; // Plus 1 to include new param *sig
    vector<uint64_t> sigs(nodeSigs);
    memcpy(&sigs[childCounts[node]], sig, sizeof(uint64_t)); // Add to end using memcpy
    
    for (int i = 0; i < childCounts[node]; i++) {
      uint64_t *currentSig = &sigs[i];
      uint64_t *matrix = &matrices[node * matrixSize];
      for (size_t j = 0; j < 64; j++) {
        currentSig[j / 64] |= ((matrix[j * matrixHeight + i / 64] >> (i % 64)) & 1) << (j % 64);
      }
    }
    
    /*
    fprintf(stderr, "Signatures converted for clustering:\n");
    for (size_t i = 0; i < nodeSigs; i++) {
      uint64_t *currentSig = &sigs[i];
      //uint64_t *currentSig = &sigs[i * signatureSize];
      dbgPrintSignature(currentSig);
    }
    */
    
    vector<uint64_t> meanSigs = createRandomSigs(rng, sigs);
    vector<size_t> clusters(nodeSigs);
    vector<vector<size_t>> clusterLists;
    for (int iteration = 0; iteration < 4; iteration++) {
      //fprintf(stderr, "Iteration %d\n", iteration);
      reclusterSignatures(clusters, meanSigs, sigs);
      clusterLists = createClusterLists(clusters);
      meanSigs = createClusterSigs(clusterLists, sigs);
    }
    
    /*
    // Display clusters (debugging purposes)
    for (const auto &clusterList : clusterLists) {
      fprintf(stderr, "Cluster:\n");
      for (size_t seqIdx : clusterList) {
        uint64_t *currentSig = &sigs[seqIdx * signatureSize];
        dbgPrintSignature(currentSig);
      }
    }
    */
    
    // Create the sibling node
    size_t sibling = getNewNodeIdx(insertionList);
    
    size_t newlyAddedIdx = childCounts[node];
    
    childCounts[sibling] = clusterLists[1].size();
    isBranchNode[sibling] = isBranchNode[node];
    {
      size_t siblingIdx = 0;
      for (size_t seqIdx : clusterLists[1]) {
        if (seqIdx < newlyAddedIdx) {
          childLinks[sibling * order + siblingIdx] = childLinks[node * order + seqIdx];
        } else {
          childLinks[sibling * order + siblingIdx] = link;
        }
        // If this is a branch node, relink the child to the new parent
        if (isBranchNode[sibling]) {
          parentLinks[childLinks[sibling * order + siblingIdx]] = sibling;
        }
        addSigToMatrix(&matrices[sibling * matrixSize], siblingIdx, &sigs[seqIdx]);
        siblingIdx++;
      }
    }
    memcpy(&means[sibling], &meanSigs[1], sizeof(uint64_t));
    
    // Fill the current node with the other cluster of signatures
    {
      fill(&matrices[node * matrixSize], &matrices[node * matrixSize] + matrixSize, 0ull);
      size_t nodeIdx = 0;
      for (size_t seqIdx : clusterLists[0]) {
        if (seqIdx < newlyAddedIdx) {
          childLinks[node * order + nodeIdx] = childLinks[node * order + seqIdx];
        } else {
          childLinks[node * order + nodeIdx] = link;
        }
        // If this is a branch node, relink the child to the new parent
        if (isBranchNode[node]) {
          parentLinks[childLinks[node * order + nodeIdx]] = node;
        }
        addSigToMatrix(&matrices[node * matrixSize], nodeIdx, &sigs[seqIdx]);
        nodeIdx++;
      }
    }
    childCounts[node] = clusterLists[0].size();
    
    // Is this the root level?
    if (node == root) {
      //fprintf(stderr, "Node being split is root node\n");
      
      // Create a new root node
      size_t newRoot;
      newRoot = getNewNodeIdx(insertionList);
      
      // Link this node and the sibling to it
      parentLinks[node] = newRoot;
      parentLinks[sibling] = newRoot;

      childCounts[newRoot] = 2;
      isBranchNode[newRoot] = 1;
      childLinks[newRoot * order + 0] = node;
      childLinks[newRoot * order + 1] = node;
      addSigToMatrix(&matrices[newRoot * matrixSize], 0, &meanSigs[0]);
      addSigToMatrix(&matrices[newRoot * matrixSize], 1, &meanSigs[1]);
      
      root = newRoot;
    } else {
      
      // First, update the reference to this node in the parent with the new mean
      size_t parent = parentLinks[node];
      
      // Lock the parent
      omp_set_lock(&locks[parent]);
      
      size_t idx = numeric_limits<size_t>::max();
      for (size_t i = 0; i < childCounts[parent]; i++) {
        if (childLinks[parent * order + i] == node) {
          idx = i;
          break;
        }
      }
      if (idx == numeric_limits<size_t>::max()) {
        //fprintf(stderr, "Error: node %zu is not its parent's (%zu) child\n", node, parent);
        
        // Abort. Unlock the parent and get out of here
        omp_unset_lock(&locks[parent]);
        return;
        
        //exit(1);
      }
      
      removeSigFromMatrix(&matrices[parent * matrixSize], idx);
      addSigToMatrix(&matrices[parent * matrixSize], idx, &meanSigs[0]);
      
      // Connect sibling node to parent
      parentLinks[sibling] = parent;
      
      // Now add a link in the parent node to the sibling node
      if (childCounts[parent] + 1 < order) {
        addSigToMatrix(&matrices[parent * matrixSize], childCounts[parent], &meanSigs[1]);
        childLinks[parent * order + childCounts[parent]] = sibling;
        childCounts[parent]++;
        
        // Update signatures (may change?)
        recalculateUp(parent);
      } else {
        splitNode(rng, parent, &meanSigs[1], insertionList, sibling);
      }
      // Unlock the parent
      omp_unset_lock(&locks[parent]);
    }
    
    //fprintf(stderr, "Split finished\n");
  }
  
  template<class RNG>
  void insert(RNG &&rng, const uint64_t *signature, vector<size_t> &insertionList)
  {
    // Warning: ALWAYS INSERT THE FIRST NODE SINGLE-THREADED
    // We don't have any protection from this because it would slow everything down to do so
    if (root == numeric_limits<size_t>::max()) {
      root = getNewNodeIdx(insertionList);
      childCounts[root] = 0;
      isBranchNode[root] = 0;
    }
    
    size_t insertionPoint = traverse(signature);
    
    //fprintf(stderr, "Inserting at %zu\n", insertionPoint);
    omp_set_lock(&locks[insertionPoint]);
    if (childCounts[insertionPoint] < order) {
      addSigToMatrix(&matrices[insertionPoint * matrixSize], childCounts[insertionPoint], signature);
      childCounts[insertionPoint]++;
    } else {
      splitNode(rng, insertionPoint, signature, insertionList, 0);
    }
    omp_unset_lock(&locks[insertionPoint]);
    
    //fprintf(stderr, "Node %zu now has %zu leaves\n", insertionPoint, childCounts[insertionPoint]);
  }
  
  void destroyLocks(size_t node)
  {
    omp_destroy_lock(&locks[node]);
    if (isBranchNode[node]) {
      for (size_t i = 0; i < childCounts[node]; i++) {
        destroyLocks(childLinks[node * order + i]);
      }
    }
  }
  void destroyLocks()
  {
    destroyLocks(root);
  }
};

void compressClusterList(vector<size_t> &clusters)
{
  unordered_map<size_t, size_t> remap;
  for (size_t &clus : clusters) {
    if (remap.count(clus)) {
      clus = remap[clus];
    } else {
      size_t newClus = remap.size();
      remap[clus] = newClus;
      clus = newClus;
    }
  }
  fprintf(stderr, "Output %zu clusters\n", remap.size());
}

vector<size_t> clusterSignatures(const vector<uint64_t> &sigs)
{
  size_t sigCount = sigs.size();
  vector<size_t> clusters(sigCount);
  KTree tree(ktree_order, ktree_capacity);
  
  vector<size_t> insertionList(1,0);
  default_random_engine rng;
  tree.insert(rng, &sigs[0], insertionList);
  
  size_t nextFree = 1;
  
  #pragma omp parallel
  {
    default_random_engine rng;
    vector<size_t> insertionList;
    
    #pragma omp for
    for (size_t i = nextFree; i < ktree_capacity; i++) {
      insertionList.push_back(ktree_capacity - i - 1);
    }
    
    #pragma omp for
    for (size_t i = 1; i < sigCount; i++) {
      tree.insert(rng, &sigs[i], insertionList);
    }
  }
  
  // We've created the tree. Now reinsert everything
  #pragma omp parallel for
  for (size_t i = 0; i < sigCount; i++) {
    size_t clus = tree.traverse(&sigs[i]);
    clusters[i] = clus;
  }
  
  // We want to compress the cluster list down
  compressClusterList(clusters);
  
  // Recursively destroy all locks
  tree.destroyLocks();
  
  return clusters;
}

int main(int argc, char **argv)
{
  if (argc < 2) {
    fprintf(stderr, "Usage: %s (options) [fasta input]\n", argv[0]);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -d [signature density]\n");
    fprintf(stderr, "  -o [tree order]\n");
    fprintf(stderr, "  -c [starting capacity]\n");
    fprintf(stderr, "  --fasta-output\n");
    return 1;
  }
  // signatureWidth = 256;
  // kmerLength = 5;
  density = 1.0f / 21.0f;
  fastaOutput = false;
  
  string fastaFile = "";
  
  for (int a = 1; a < argc; a++) {
    string arg(argv[a]);
    if (arg == "-d") density = atof(argv[++a]);
    else if (arg == "-o") ktree_order = atoi(argv[++a]);
    else if (arg == "-c") ktree_capacity = atoi(argv[++a]);
    else if (arg == "--fasta-output") fastaOutput = true;
    else if (fastaFile.empty()) fastaFile = arg;
    else {
      fprintf(stderr, "Invalid or extra argument: %s\n", arg.c_str());
      exit(1);
    }
  }
    
  if (density < 0.0f || density > 1.0f) {
    fprintf(stderr, "Error: density must be a positive value between 0 and 1\n");
    return 1;
  }
  
  fprintf(stderr, "Loading fasta...");
  auto fasta = loadFasta(fastaFile.c_str());
  fprintf(stderr, " loaded %llu sequences\n", static_cast<unsigned long long>(fasta.size()));
  fprintf(stderr, "Converting fasta to signatures...");
  auto sigs = convertFastaToSignatures(fasta);
  fprintf(stderr, " done\n");
  fprintf(stderr, "Clustering signatures...\n");
  auto clusters = clusterSignatures(sigs);
  fprintf(stderr, "Writing output\n");
  if (!fastaOutput) {
    outputClusters(clusters);
  } else {
    outputFastaClusters(clusters, fasta);
  }
  
  return 0;
}
