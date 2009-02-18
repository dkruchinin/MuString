/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.berlios.de>
 * (c) Copyright 2008 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * ds/ttree.c: T*-tree implementation.
 *
 * For more information about T- and T*-trees see:
 * 1) Tobin J. Lehman , Michael J. Carey,
 *    A Study of Index Structures for Main Memory Database Management Systems
 * 2) Kong-Rim Choi , Kyung-Chang Kim,
 *    T*-tree: a main memory database index structure for real time applications
 */

#include <config.h>
#include <ds/iterator.h>
#include <ds/ttree.h>
#include <mm/slab.h>
#include <mlibc/string.h>
#include <mlibc/assert.h>
#include <mlibc/kprintf.h>
#include <mlibc/stddef.h>
#include <mlibc/types.h>

/* Index number of first key in a T*-tree node when a node has only one key. */
#define first_tnode_idx(ttree)                  \
  (((ttree)->keys_per_tnode >> 1) - 1)

/*
 * Minimum allowed number of used rooms in a T*-tree node.
 * By default it's a quoter of total number of key rooms in a node.
 */
#define min_tnode_entries(ttree)                \
  ((ttree)->keys_per_tnode - ((ttree)->keys_per_tnode >> 2))

/*
 * T*-tree has three types of node:
 * 1. Node that hasn't left and right child is called "leaf node".
 * 2. Node that has only one child is called "half-leaf node"
 * 3. Finally, node that has both left and right childs is called "internal node"
 */
#define is_leaf_node(node)                      \
  (!(node)->left && !(node)->right)
#define is_internal_node(node)                  \
  ((node)->left && (node)->right)
#define is_half_leaf(tnode)                     \
  ((!(tnode)->left || !(tnode)->right) && !((tnode)->left && (tnode)->right))

/* Translate node side to balance factor */
#define side2bfc(side)                          \
  __balance_factors[side]
#define get_bfc_delta(node)                     \
  (side2bfc(tnode_get_side(node)))
#define subtree_is_unbalanced(node)             \
  (((node)->bfc < -1) || ((node)->bfc > 1))
#define opposite_side(side)                     \
  (!(side))
#define left_heavy(node)                        \
  ((node)->bfc < 0)
#define right_heavy(node)                       \
  ((node)->bfc > 0)

struct tnode_lookup {
  void *key;
  int low_bound;
  int high_bound;  
};

static int __balance_factors[] = { -1, 1 };
static memcache_t *__tnodes_memcache = NULL;

#ifdef CONFIG_DEBUG_TTREE
#define TT_VERBOSE(fmt, args...)                \
  do {                                          \
    kprintf(KO_DEBUG "[T*-tree]: ");            \
    kprintf(fmt, ##args);                       \
  } while (0)

int ttree_check_depth_dbg(ttree_node_t *tnode)
{
  int l, r;
  
  if (!tnode)
    return 0;

  l = ttree_check_depth_dbg(tnode->left);
  r = ttree_check_depth_dbg(tnode->right);
  if (tnode->left)
    l++;
  if (tnode->right)
    r++;
  if ((r - l) >= 1)
    panic("T*-tree is right-heavy, but BFC is %d. tnode = %p", tnode->bfc, tnode);
  else if ((r - l) <= -1)
    panic("T*-tree is left-heavy, but BFC is %d. tnode = %p", tnode->bfc, tnode);

  return ((r > l) ? r : l);
}

void ttree_display_cursor_dbg(ttree_cursor_t *cursor)
{
  kprintf("=== Cursor content ===\n");
  kprintf("\t.ttree = %p\n", cursor->ttree);
  kprintf("\t.tnode = %p", cursor->tnode);
  if (cursor->tnode)
    kprintf(" [%d, %d]", cursor->tnode->min_idx, cursor->tnode->max_idx);

  kprintf("\n");
  kprintf("\t.idx = %d\n", cursor->idx);
  kprintf("\t.side = ");
  switch (cursor->side) {
      case TNODE_BOUND:
        kprintf("TNODE_BOUND");
        break;
      case TNODE_LEFT:
        kprintf("TNODE_LEFT");
        break;
      case TNODE_RIGHT:
        kprintf("TNODE_BOUND");
        break;
      default:
        kprintf("Unknown side type");
        break;
  }

  kprintf("\n");
  kprintf("\t.state = ");
  switch (cursor->state) {
      case TT_CSR_UNTIED:
        kprintf("TT_CSR_UNTIED");
        break;
      case TT_CSR_TIED:
        kprintf("TT_CSR_TIED");
        break;
      case TT_CSR_PENDING:
        kprintf("TT_CSR_PENDING");
        break;
      default:
        kprintf("Unknown cursor state");
        break;
  }

  kprintf("\n=========\n");
}
#else
#define TT_VERBOSE(fmt, args...)
#endif /* CONFIG_DEBUG_TTREE */


static ttree_node_t *allocate_ttree_node(ttree_t *ttree)
{
  ttree_node_t *tnode = alloc_from_memcache(__tnodes_memcache);
  if (!tnode)
    panic("allocate_ttree_node: Can't allocate new T*-tree node. ENOMEM.");
  
  memset(tnode, 0, sizeof(*tnode) - 2 * sizeof(uintptr_t));
  return tnode;
}

/*
 * T*-tree node contains keys in a sorted order. Thus binary search
 * is used for internal lookup.
 */
static void *lookup_inside_tnode(ttree_t *ttree, ttree_node_t *tnode, struct tnode_lookup *tnl, int *out_idx)
{
  int floor, ceil, mid, cmp_res;

  floor = tnl->low_bound;
  ceil = tnl->high_bound;
  TT_ASSERT_DBG((floor >= 0) && (ceil < ttree->keys_per_tnode));
  while (floor <= ceil) {
    mid = (floor + ceil) >> 1;
    if ((cmp_res = ttree->cmp_func(tnl->key, tnode->keys[mid])) < 0)
      ceil = mid - 1;
    else if (cmp_res > 0)
      floor = mid + 1;
    else {
      *out_idx = mid;
      return ttree_key2item(ttree, tnode->keys[mid]);
    }
  }

  /*
   * If a key position is not found, save an index of the position
   * where key may be placed to.
   */
  *out_idx = floor;
  return NULL;
}

static inline void increase_tnode_window(ttree_t *ttree, ttree_node_t *tnode, int *idx)
{
  register int i;

  /*
   * If the right side of an array has more free rooms than the left one,
   * the window will grow to the right. Otherwise it'll grow to the left.
   */
  if ((ttree->keys_per_tnode - 1 - tnode->max_idx) > tnode->min_idx) {
    for (i = ++tnode->max_idx; i > *idx - 1; i--)
      tnode->keys[i] = tnode->keys[i - 1];    
  }
  else {
    *idx -= 1;
    for (i = --tnode->min_idx; i < *idx; i++)
      tnode->keys[i] = tnode->keys[i + 1];    
  }
}

static inline void decrease_tnode_window(ttree_t *ttree, ttree_node_t *tnode, int *idx)
{
  register int i;

  /* Shrink the window to the longer side by given index. */
  if ((ttree->keys_per_tnode - 1 - tnode->max_idx) <= tnode->min_idx) {
    tnode->max_idx--;
    for (i = *idx; i <= tnode->max_idx; i++)
      tnode->keys[i] = tnode->keys[i + 1];
  }
  else {
    tnode->min_idx++;
    for (i = *idx; i >= tnode->min_idx; i--)
      tnode->keys[i] = tnode->keys[i - 1];

    *idx = *idx + 1;
  }
}

/*
 * generic single rotation procedure.
 * side = TNODE_LEFT  - Right rotation
 * side = TNODE_RIGHT - Left rotation.
 * "target" will be set to the new root of rotated subtree.
 */
static void __rotate_single(ttree_node_t **target, int side)
{
  ttree_node_t *p, *s;
  int opside = opposite_side(side);

  p = *target;
  TT_ASSERT_DBG(p != NULL);
  s = p->sides[side];
  TT_ASSERT_DBG(s != NULL);
  tnode_set_side(s, tnode_get_side(p));
  p->sides[side] = s->sides[opside];  
  s->sides[opside] = p;
  tnode_set_side(p, opside);
  s->parent = p->parent;  
  p->parent = s;
  if (p->sides[side]) {
    p->sides[side]->parent = p;
    tnode_set_side(p->sides[side], side);
  }
  if (s->parent) {
    if (s->parent->sides[side] == p)
      s->parent->sides[side] = s;
    else
      s->parent->sides[opside] = s;
  }

  *target = s;
}

/*
 * There are two cases of single rotation possible:
 * 1) Right rotation (side = TNODE_LEFT)
 *         [P]             [L]
 *        /  \            /  \
 *      [L]  x1    =>   x2   [P]
 *     /  \                 /  \
 *    x2  x3               x3  x1
 *
 * 2) Left rotation (side = TNODE_RIHGT)
 *      [P]                [R]
 *     /  \               /  \
 *    x1  [R]      =>   [P]   x2
 *       /  \          /  \
 *     x3   x2        x1  x3
 */
static void rotate_single(ttree_node_t **target, int side)
{
  ttree_node_t *n;

  __rotate_single(target, side);
  n = (*target)->sides[opposite_side(side)];

  /*
   * Recalculate balance factors of nodes after rotation.
   * Let X was a root node of rotated subtree and Y was its
   * child. After single rotation Y is new root of subtree and X is its child.
   * Y node may become either balanced or overweighted to the
   * same side it was but 1 level less.
   * X node scales at 1 level down and possibly it has new child, so
   * its balance should be recalculated too. If it still internal node and
   * its new parent was not overwaighted to the opposite to X side,
   * X is overweighted to the opposite to its new parent side, otherwise it's balanced.
   * If X is either half-leaf or leaf, balance racalculation is obvious.
   */
  if (is_internal_node(n))
    n->bfc = (n->parent->bfc != side2bfc(side)) ? side2bfc(side) : 0;
  else
    n->bfc = !!(n->right) - !!(n->left);
  
  (*target)->bfc += side2bfc(opposite_side(side));
  TT_ASSERT_DBG((ABS(n->bfc < 2) && (ABS((*target)->bfc) < 2)));
}

/*
 * There are two possible cases of double rotation:
 * 1) Left-right rotation: (side == TNODE_LEFT)
 *      [P]                     [r]
 *     /  \                    /  \
 *   [L]  x1                [L]   [P]
 *  /  \          =>       / \    / \
 * x2  [r]                x2 x4  x3 x1
 *    /  \
 *  x4   x3
 *
 * 2) Right-left rotation: (side == TNODE_RIGHT)
 *      [P]                     [l]
 *     /  \                    /  \
 *    x1  [R]               [P]   [R]
 *       /  \     =>        / \   / \
 *      [l] x2             x1 x3 x4 x2
 *     /  \
 *    x3  x4
 */  
static void rotate_double(ttree_node_t **target, int side)
{
  int opside = opposite_side(side);
  ttree_node_t *n = (*target)->sides[side];
  
  __rotate_single(&n, opside);
  /* Balance recalculation is very similar to recalculation after simple single rotation. */
  if (is_internal_node(n->sides[side]))
    n->sides[side]->bfc = (n->bfc == side2bfc(opside)) ? side2bfc(side) : 0;
  else
    n->sides[side]->bfc = !!(n->sides[side]->right) - !!(n->sides[side]->left);

  TT_ASSERT_DBG(ABS(n->sides[side]->bfc) < 2);
  n = n->parent;
  __rotate_single(target, side);
  if (is_internal_node(n))
    n->bfc = ((*target)->bfc == side2bfc(side)) ? side2bfc(opside) : 0;
  else
    n->bfc = !!(n->right) - !!(n->left);
  
  /* new root node of subtree is always ideally balanced after double rotation. */
  TT_ASSERT_DBG(ABS(n->bfc) < 2);
  (*target)->bfc = 0;
}

static void rebalance(ttree_t *ttree, ttree_node_t **node, ttree_cursor_t *cursor)
{
  int lh = left_heavy(*node);
  int sum = ABS((*node)->bfc + (*node)->sides[opposite_side(lh)]->bfc);

  
  if (sum >= 2) {
    rotate_single(node, opposite_side(lh));
    goto out;
  }

  rotate_double(node, opposite_side(lh));

  /*
   * T-tree rotation rules difference from AVL rules in only one aspect.
   * After double rotation is done and a leaf became new root node of subtree
   * and both its left and right childs are half-leafs. If the new root node
   * contains only one item, N - 1 items should be moved to it from one of its childs.
   * (N is a number of items in selected child node).
   */
  if ((tnode_num_keys(*node) == 1) &&
      is_half_leaf((*node)->left) && is_half_leaf((*node)->right)) {
    ttree_node_t *n;
    int offs, nkeys;

    /*
     * If right child contains more items than left, they will be moved
     * from the right child. Otherwise from the left one. 
     */
    if (tnode_num_keys((*node)->right) >= tnode_num_keys((*node)->left)) {
      /*
       * Right child was selected. So first N - 1 items will be copied
       * and inserted after parent's first item.
       */
      n = (*node)->right;
      nkeys = tnode_num_keys(n);
      (*node)->keys[0] = (*node)->keys[(*node)->min_idx];
      offs = 1;
      (*node)->min_idx = 0;
      (*node)->max_idx = nkeys - 1;
      if (cursor->tnode == n) {
        TT_VERBOSE("(R) Tree cursor was affected due rebalancing and values of its members will be changed.\n"
                   " Current values: { .tnode = %p, .idx = %d, .side = %d }\n",
                   cursor->tnode, cursor->idx, cursor->side);
        if (cursor->idx < n->max_idx) {
          cursor->tnode = *node;
          cursor->idx = (*node)->min_idx + (cursor->idx - n->min_idx + 1);
        }
        else
          cursor->idx = first_tnode_idx(ttree);

        TT_VERBOSE("(R) New values of cursor: { .tnode = %p, .idx = %d, .side = %d }\n",
                   cursor->tnode, cursor->idx, cursor->side);
      }
    }
    else {
      /*
       * Left child was selected. So its N - 1 items(starting after the min one)
       * will be copied and inserted before parent's single item.
       */
      n = (*node)->left;
      nkeys = tnode_num_keys(n);
      (*node)->keys[ttree->keys_per_tnode - 1] = (*node)->keys[(*node)->min_idx];
      (*node)->min_idx = offs = ttree->keys_per_tnode - nkeys;
      (*node)->max_idx = ttree->keys_per_tnode - 1;
      if (cursor->tnode == n) {
        TT_VERBOSE("(L) Tree cursor was affected due rebalancing and values of its members will be changed.\n"
                   " Current values: { .tnode = %p, .idx = %d, .side = %d }\n",
                   cursor->tnode, cursor->idx, cursor->side);
        if (cursor->idx > n->min_idx) {
          cursor->tnode = *node;
          cursor->idx = (*node)->min_idx + (cursor->idx - n->min_idx);
        }
        else
          cursor->idx = first_tnode_idx(ttree);

        TT_VERBOSE("(L) New values of cursor: { .tnode = %p, .idx = %d, .side = %d }\n",
                   cursor->tnode, cursor->idx, cursor->side);
      }
      
      n->max_idx = n->min_idx++;
    }

    memcpy((*node)->keys + offs,
           n->keys + n->min_idx, sizeof(void *) * (nkeys - 1));
    n->keys[first_tnode_idx(ttree)] = n->keys[n->max_idx];
    n->min_idx = n->max_idx = first_tnode_idx(ttree);
  }

  out:
  if (ttree->root->parent)
    ttree->root = *node;
}

static inline void __add_successor(ttree_node_t *n)
{
  /*
   * After new leaf node was added, its successor should be
   * fixed. Also it(successor) could became a successor of the node 
   * higher than the given one.
   * There are several possible cases of such situation:
   * 1) If new node is added as a right child, it inherites
   *    successor of its parent. And it itself becomes a successor of its parent.
   * 2) If it is a left child, its parent will be the successor.
   * 2.1) If parent itself is a right child, then newly added node becomes
   *      the successor of parent's parent.
   * 2.2) Otherwise it becomes a successor of one of nodes located higher.
   *      In this case, we should browse up the tree starting from parent's parent.
   *      One of the nodes on the path *may* have a successor equals to parent of a
   *      newly added node. If such node will be found, its successor should be changed
   *      to a newly added node.
   */
  if (tnode_get_side(n) == TNODE_RIGHT) {
    n->successor = n->parent->successor;
    n->parent->successor = n;
  }
  else {
    n->successor = n->parent;
    if (tnode_get_side(n->parent) == TNODE_RIGHT)
      n->parent->parent->successor = n;
    else if (tnode_get_side(n->parent) == TNODE_LEFT) {
      register ttree_node_t *node;

      for (node = n->parent->parent; node; node = node->parent) {
        if (node->successor == n->parent) {
          node->successor = n;
          break;
        }
      }
    }
  }
}

static inline void __remove_successor(ttree_node_t *n)
{
  /*
   * Node removing could affect the successor of one of nodes with higher level,
   * so it should be fixed. Since T*-tree node deletion algorithm
   * assumes that ony leafs are removed, successor fixing
   * is opposite to successor adding algorithm.
   */
  if (tnode_get_side(n) == TNODE_RIGHT)
    n->parent->successor = n->successor;
  else if (tnode_get_side(n->parent) == TNODE_RIGHT)
    n->parent->parent->successor = n->parent;
  else {
    register ttree_node_t *node = n;
      
    while ((node = node->parent)) {
      if (node->successor == n) {
        node->successor = n->parent;
        break;
      }
    }
  }
}

static void fixup_after_insertion(ttree_t *ttree, ttree_node_t *n, ttree_cursor_t *cursor)
{
  int bfc_delta = get_bfc_delta(n);
  ttree_node_t *node = n;

  __add_successor(n);
  /* check tree for balance after new node was added. */
  while ((node = node->parent)) {
    node->bfc += bfc_delta;
    /*
     * if node becomes balanced, tree balance is ok,
     * so process may be stopped here
     */
    if (!node->bfc)
      return;
    if (subtree_is_unbalanced(node)) {
      /*
       * Because of nature of T-tree rebalancing, just inserted item
       * may change its position in its node and even the node itself.
       * Thus if T-tree cursor was specified we have to take care of it.
       */
      rebalance(ttree, &node, cursor);
      /* single or double rotation tree becomes balanced and we can stop here. */
      return;
    }
    
    bfc_delta = get_bfc_delta(node);
  }
}

static void fixup_after_deletion(ttree_t *ttree, ttree_node_t *n, ttree_cursor_t *cursor)
{
  ttree_node_t *node = n->parent;
  int bfc_delta = get_bfc_delta(n);

  __remove_successor(n);
  /* Unlike balance fixing after insertion, deletion may require several rotations. */
  while (node) {    
    node->bfc -= bfc_delta;
    /*
     * If node's balance factor was 0 and becomes 1 or -1, we can stop.
     */
    if (!(node->bfc + bfc_delta))
      break;

    bfc_delta = get_bfc_delta(node); 
    if (subtree_is_unbalanced(node)) {
      ttree_node_t *tmp = node;
      
      rebalance(ttree, &tmp, cursor);
      /*
       * If after rotation subtree height is not changed,
       * proccess should be continued.
       */
      if (tmp->bfc)
        break;
      
      node = tmp;
    }
    
    node = node->parent;
  }
}

void __ttree_init(ttree_t *ttree, ttree_cmp_func_t cmpf, size_t key_offs)
{
  CT_ASSERT((TTREE_DEFAULT_NUMKEYS >= TNODE_ITEMS_MIN) &&
            (TTREE_DEFAULT_NUMKEYS <= TNODE_ITEMS_MAX));
  ttree->root = NULL;
  ttree->keys_per_tnode = TTREE_DEFAULT_NUMKEYS;
  ttree->cmp_func = cmpf;
  ttree->key_offs = key_offs;
  CT_ASSERT(tnode_size(ttree) <= SLAB_OBJECT_MAX_SIZE);
  if (!__tnodes_memcache) {
    __tnodes_memcache = create_memcache("T*-tree nodes cache", tnode_size(ttree), 1, 0);
    if (!__tnodes_memcache)
      panic("__ttree_init: Couldn't create memory cache for T*-tree nodes cache. ENOMEM.");
  }
}

void ttree_destroy(ttree_t *ttree)
{
  ttree_node_t *tnode, *next;

  if (!ttree->root)
    return;
  for (tnode = next = ttree_tnode_leftmost(ttree->root); tnode; tnode = next) {
    next = tnode->successor;
    memfree(tnode);
  }
}

void *ttree_lookup(ttree_t *ttree, void *key, ttree_cursor_t *cursor)
{
  ttree_node_t *n, *marked_tn, *target;
  int side = TNODE_BOUND, cmp_res, idx;
  void *item = NULL;
  enum ttree_cursor_state st = TT_CSR_UNTIED;

  /*
   * Classical T-tree search algorithm is O(log(2N/M) + log(M - 2))
   * Where N is total number of items in the tree and M is a number of
   * items per node. In worst case each node on the path requires 2
   * comparison(with its min and max items) plus binary search in the last
   * node(bound node) excluding its first and last items.
   *
   * Here is used another approach that was suggested in
   * "Tobin J. Lehman , Michael J. Carey, A Study of Index Structures for Main Memory Database Management Systems".
   * It reduces O(log(2N/M) + log(M - 2)) to true O(log(N)). This algorithm compares the search
   * key only with minimum item in each node. If search key is greater, current node is marked
   * for future consideration.
   */
  target = n = ttree->root;
  marked_tn = NULL;
  idx = first_tnode_idx(ttree);
  if (!n)
    goto out;
  while (n) {
    target = n;
    cmp_res = ttree->cmp_func(key, tnode_key_min(n));
    if (cmp_res < 0)
      side = TNODE_LEFT;
    else if (cmp_res > 0) {      
      marked_tn = n; /* mark current node for future consideration. */
      side = TNODE_RIGHT;
    }
    else { /* ok, key is found, search is completed. */
      side = TNODE_BOUND;
      idx = n->min_idx;
      item = ttree_key2item(ttree, tnode_key_min(n));
      st = TT_CSR_TIED;
      goto out;
    }
    
    n = n->sides[side];
  }
  if (marked_tn) {
    int c = ttree->cmp_func(key, tnode_key_max(marked_tn));
    
    if (c <= 0) {
      side = TNODE_BOUND;
      target = marked_tn;
      if (!c) {
        item = ttree_key2item(ttree, tnode_key_max(target));
        idx = target->max_idx;
        st = TT_CSR_TIED;
      }
      else { /* make internal binary search */
        struct tnode_lookup tnl;

        tnl.key = key;
        tnl.low_bound = target->min_idx + 1;
        tnl.high_bound = target->max_idx - 1;
        item = lookup_inside_tnode(ttree, target, &tnl, &idx);
        st = (item != NULL) ? TT_CSR_TIED : TT_CSR_PENDING;
      }

      goto out;
    }
  }

  /*
   * If we're here, item wasn't found. So the only thing
   * need to be done is to determine the position where search key
   * may be placed to. If target node is not empty, key may be placed
   * on its min or max positions.
   */
  if (!tnode_is_full(ttree, target)) {
    side = TNODE_BOUND;
    idx = ((marked_tn != target) || (cmp_res < 0)) ?
      target->min_idx : (target->max_idx + 1);
    st = TT_CSR_PENDING;
  }

  out:
  if (cursor) {
    ttree_cursor_init(ttree, cursor);
    cursor->tnode = target;
    cursor->side = side;
    cursor->idx = idx;
    cursor->state = st;
  }

  return item;
}

int ttree_insert(ttree_t *ttree, void *item)
{
  ttree_cursor_t cursor;

  if (ttree_lookup(ttree, ttree_item2key(ttree, item), &cursor))
    return -1;

  ttree_insert_placeful(&cursor, item);
  return 0;
}

void ttree_insert_placeful(ttree_cursor_t *cursor, void *item)
{
  ttree_t *ttree = cursor->ttree;
  ttree_node_t *at_node, *n;
  void *key;

  TT_ASSERT_DBG(cursor->ttree != NULL);
  key = ttree_item2key(ttree, item);
  n = at_node = cursor->tnode;
  cursor->state = TT_CSR_TIED;
  if (!ttree->root) { /* The root node has to be created. */
    at_node = allocate_ttree_node(ttree);
    at_node->keys[first_tnode_idx(ttree)] = key;
    at_node->min_idx = at_node->max_idx = first_tnode_idx(ttree);
    ttree->root = at_node;
    tnode_set_side(at_node, TNODE_ROOT);
    cursor->tnode = at_node;
    cursor->idx = at_node->min_idx;
    cursor->side = TNODE_BOUND;
    return;
  }

  __validate_cursor_dbg(cursor);  
  if (cursor->side == TNODE_BOUND) {
    if (tnode_is_full(ttree, n)) {
      /*
       * If node is full its max item should be removed and
       * new key should be inserted into it. Removed key becomes
       * new insert value that should be put in successor node.
       */
      void *tmp = n->keys[n->max_idx--];

      increase_tnode_window(ttree, n, &cursor->idx);
      n->keys[cursor->idx] = key;
      key = tmp;
      
      /*
       * If current node hasn't successor and right child
       * New node have to be created. It'll become the right child of the current node.
       */
      if (!n->successor || !n->right) {
        cursor->side = TNODE_RIGHT;
        cursor->idx = first_tnode_idx(ttree);
        goto create_new_node;
      }

      at_node = n->successor;
      /*
       * If successor hasn't any free rooms, new value is inserted
       * into newly created node that becomes left child of the current
       * node's successor.
       */
      if (tnode_is_full(ttree, at_node)) {
        cursor->side = TNODE_LEFT;
        cursor->idx = first_tnode_idx(ttree);
        goto create_new_node;
      }

      /*
       * If we're here, then successor has free rooms and key
       * will be inserted to one of them.
       */
      cursor->idx = at_node->min_idx;
      cursor->tnode = at_node;
    }

    increase_tnode_window(ttree, at_node, &cursor->idx);
    at_node->keys[cursor->idx] = key;
    return;
  }

  create_new_node:
  n = allocate_ttree_node(ttree);
  n->keys[cursor->idx] = key;
  n->min_idx = n->max_idx = cursor->idx;
  n->parent = at_node;
  at_node->sides[cursor->side] = n;  
  tnode_set_side(n, cursor->side);
  cursor->tnode = n;  
  fixup_after_insertion(ttree, n, cursor);  
}

void *ttree_delete(ttree_t *ttree, void *key)
{
  ttree_cursor_t cursor;
  void *ret;

  ret = ttree_lookup(ttree, key, &cursor);
  if (!ret)
    return ret;

  ttree_delete_placeful(&cursor);
  return ret;
}

void *ttree_delete_placeful(ttree_cursor_t *cursor)
{
  ttree_t *ttree = cursor->ttree;
  ttree_node_t *tnode, *n;
  void *ret;

  __validate_cursor_dbg(cursor);
  tnode = cursor->tnode;
  ret = ttree_key2item(ttree, tnode->keys[cursor->idx]);
  decrease_tnode_window(ttree, tnode, &cursor->idx);

  /*
   * If after a key was removed, T*-tree node contains more than
   * minimum allowed number of items, the proccess is completed.
   */
  if (tnode_num_keys(tnode) > min_tnode_entries(ttree))
    return ret;
  if (is_internal_node(tnode)) {
    int idx;

    /*
     * If it is an internal node, recovery number of items in it
     * by moving one item from its successor.
     */
    n = tnode->successor;
    idx = tnode->max_idx + 1;
    increase_tnode_window(ttree, tnode, &idx);
    tnode->keys[idx] = n->keys[n->min_idx++];
    if (unlikely(cursor->idx > tnode->max_idx))
      cursor->idx = tnode->max_idx;    
    if (!tnode_is_empty(n) && is_leaf_node(n))
      return ret;

    /*
     * If we're here, then successor is either a half-leaf
     * or an empty leaf.
     */
    tnode = n;
  }
  if (!is_leaf_node(tnode)) {
    int items, diff;

    n = tnode->left ? tnode->left : tnode->right;
    items = tnode_num_keys(n);
    
    /* If half-leaf can not be merged with a leaf, the proccess is completed. */
    if (items > (ttree->keys_per_tnode - tnode_num_keys(tnode)))
      return ret;
    
    if (tnode_get_side(n) == TNODE_RIGHT) {
      /*
       * Merge current node with its right leaf. Items from the leaf
       * are placed after the maximum item in a node.
       */
      diff = (ttree->keys_per_tnode - tnode->max_idx - items) - 1;      
      if (diff < 0) {
        memcpy(tnode->keys + tnode->min_idx + diff,
               tnode->keys + tnode->min_idx, sizeof(void *) * tnode_num_keys(tnode));
        tnode->min_idx += diff;
        tnode->max_idx += diff;
        if (cursor->tnode == tnode)
          cursor->idx += diff;
      }
      memcpy(tnode->keys + tnode->max_idx + 1, n->keys + n->min_idx, sizeof(void *) * items);
      tnode->max_idx += items;
    }
    else {
      /*
       * Merge current node with its left leaf. Items the leaf
       * are placed before the minimum item in a node.
       */
      diff = tnode->min_idx - items;
      if (diff < 0) {
        register int i;
        
        for (i = tnode->max_idx; i >= tnode->min_idx; i--)
          tnode->keys[i - diff] = tnode->keys[i];

        tnode->min_idx -= diff;
        tnode->max_idx -= diff;
        if (cursor->tnode == tnode)
          cursor->idx -= diff;
      }

      memcpy(tnode->keys + tnode->min_idx - items, n->keys + n->min_idx, sizeof(void *) * items);
      tnode->min_idx -= items;
    }

    n->min_idx = 1;
    n->max_idx = 0;
    tnode = n;    
  }
  if (!tnode_is_empty(tnode))
    return ret;
  if (cursor->tnode == tnode) {
    if (tnode->successor) {
      cursor->tnode = tnode->successor;
      cursor->side = TNODE_BOUND;
      cursor->idx = cursor->tnode->min_idx;
      cursor->state = TT_CSR_TIED;
    }
    else {
      cursor->tnode = NULL;
      cursor->state = TT_CSR_UNTIED;
    }
  }
  
  /* if we're here, then current node will be removed from the tree. */
  n = tnode->parent;
  if (!n) {
    ttree->root = NULL;
    memfree(tnode);
    return ret;
  }

  n->sides[tnode_get_side(tnode)] = NULL;
  fixup_after_deletion(ttree, tnode, cursor);
  memfree(tnode);
  return ret;
}

int ttree_replace(ttree_t *ttree, void *key, void *new_item)
{
  ttree_cursor_t cursor;

  if (!ttree_lookup(ttree, &cursor, key))
    return -1;

  cursor.tnode->keys[cursor.idx] = ttree_item2key(ttree, new_item);
  return 0;
}

void ttree_cursor_init(ttree_t *ttree, ttree_cursor_t *cursor)
{
  cursor->ttree = ttree;
  cursor->tnode = NULL;
  cursor->side = TNODE_BOUND;
  cursor->idx = first_tnode_idx(cursor->ttree);
  cursor->state = TT_CSR_UNTIED;
}

int ttree_cursor_next(ttree_cursor_t *cursor)
{
  if (unlikely(cursor->state == TT_CSR_UNTIED))
    return -1;
  
  __validate_cursor_dbg(cursor);
  if (unlikely(cursor->state == TT_CSR_PENDING)) {
    int ret = 0;
    
    if ((cursor->side == TNODE_LEFT) || (cursor->idx < cursor->tnode->min_idx)) {
      cursor->side = TNODE_BOUND;
      cursor->idx = cursor->tnode->min_idx;
      cursor->state = TT_CSR_TIED;
    }
    else if (cursor->idx == cursor->tnode->max_idx)
      cursor->state = TT_CSR_TIED;
    else
      ret = -1;

    return ret;
  }
  if (unlikely(cursor->idx == cursor->tnode->max_idx)) {
    if (cursor->tnode->successor) {
      cursor->tnode = cursor->tnode->successor;
      cursor->idx = cursor->tnode->min_idx;
      cursor->side = TNODE_BOUND;
      cursor->state = TT_CSR_TIED;
      return 0;
    }
    
    if (likely(tnode_is_full(cursor->ttree, cursor->tnode))) {
      cursor->side = TNODE_RIGHT;
      cursor->idx = first_tnode_idx(cursor->ttree);
    }
    else {      
      cursor->side = TNODE_BOUND;
      cursor->idx++;
    }

    cursor->state = TT_CSR_PENDING;
    return -1;
  }

  cursor->idx++;  
  cursor->side = TNODE_BOUND;
  cursor->state = TT_CSR_TIED;  
  return 0;
}

int ttree_cursor_prev(ttree_cursor_t *cursor)
{
  if (unlikely(cursor->state == TT_CSR_UNTIED))
    return -1;

  __validate_cursor_dbg(cursor);
  if (unlikely(cursor->state == TT_CSR_PENDING)) {
    if ((cursor->side == TNODE_RIGHT) || (cursor->idx > cursor->tnode->max_idx)) {
      cursor->side = TNODE_BOUND;
      cursor->idx = cursor->tnode->max_idx;
      cursor->state = TT_CSR_TIED;
      return 0;
    }
    else if ((cursor->side == TNODE_LEFT) || (cursor->idx < cursor->tnode->min_idx)) {
      cursor->side = TNODE_BOUND;
      cursor->idx = cursor->tnode->min_idx;
    }
  }
  if (unlikely(cursor->idx == cursor->tnode->min_idx)) {
    ttree_node_t *n = ttree_tnode_glb(cursor->tnode);

    if (unlikely(n == NULL)) {
      if (likely(cursor->tnode->parent != NULL)) {
        for (n = cursor->tnode->parent; n->parent &&
               (tnode_get_side(n) == TNODE_LEFT); n = n->parent);
        n = n->parent;
      }
      else
        goto no_prev;
    }
    if (likely(n != NULL)) {
      cursor->tnode = n;
      cursor->idx = cursor->tnode->max_idx;
      cursor->side = TNODE_BOUND;
      cursor->state = TT_CSR_TIED;
      return 0;
    }

    no_prev:
    cursor->state = TT_CSR_PENDING;
    if (!cursor->idx) {
      cursor->side = TNODE_LEFT;
      cursor->idx = first_tnode_idx(cursor->ttree);
    }
    else {
      cursor->side = TNODE_BOUND;
      cursor->idx--;
    }
    
    return -1;
  }

  cursor->side = TNODE_BOUND;
  cursor->state = TT_CSR_TIED;
  cursor->idx--;
  return 0;
}

static void __print_tree(ttree_node_t *tnode, int offs, void (*fn)(ttree_node_t *tnode))
{
  int i;

  for (i = 0; i < offs; i++)
    kprintf(" ");
  if (!tnode) {
    kprintf("(nil)\n");
    return;
  }
  if (tnode_get_side(tnode) == TNODE_LEFT)
    kprintf("[L] ");
  else if (tnode_get_side(tnode) == TNODE_RIGHT)
    kprintf("[R] ");
  else
    kprintf("[*] ");

  kprintf("\n");
  for (i = 0; i < offs + 1; i++)
    kprintf(" ");

  kprintf("<%d> ", tnode_num_keys(tnode));
  fn(tnode);
  __print_tree(tnode->left, offs + 1, fn);
  __print_tree(tnode->right, offs + 1, fn);
}

void ttree_print(ttree_t *ttree, void (*fn)(ttree_node_t *tnode))
{
  __print_tree(ttree->root, 0, fn);
}

