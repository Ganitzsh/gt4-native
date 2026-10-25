struct Node;
extern "C" Node *func_0025C300(Node *n);
extern "C" Node *func_002D7EB8(void *self, void *pos, Node *first, Node *last, int count);

extern "C" Node *func_002D7F98(void *self, void *pos, Node *first, Node *last) {
    int count = 0;
    for (Node *p = first; p != last; p = func_0025C300(p))
        count++;
    return func_002D7EB8(self, pos, first, last, count);
}
/* native correction of src/ui/unit_002D7D28/func_002D7F98.cpp: returns the Node * that the tail call func_002D7EB8 leaves in $v0 (callee and its declaration made non-void). */
