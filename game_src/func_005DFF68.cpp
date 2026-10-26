/* Native stand-in for 005DFF68 and 005E07F0 (SGI STL sort, the introsort loop and the final
 * insertion sort, instantiated for the widget-focus records of 00270118: 44-byte Record, 0x54-byte
 * comparator whose operator() is func_0026F060). The matched sources compile natively, but gcc 2.9
 * (i386) passes the wrong temporary when the insertion step (005E18E8) calls 005E2F30 with two
 * aggregates by value (the comparator copy went where the element copy belongs; the comparator then
 * read a widget pointer of 1). Here the records are sorted by an insertion sort that passes
 * pointers: the comparator is called as the original does, (comparator, a, b), and ties are broken by
 * the comparator itself, so the order is the original's. 005DFF68 does the whole sort; 005E07F0 (the
 * final pass the original adds) then finds the records sorted. Aggregates passed by value with a
 * non-trivial copy are passed by address (gcc's invisible reference), as callers compile them. */
struct Record { int w[11]; };
struct Compare;
extern "C" int func_0026F060(Compare *comp, const Record *a, const Record *b);

static void insertion_sort(Record *first, Record *last, Compare *comp) {
    if (first == last) return;
    for (Record *i = first + 1; i < last; ++i) {
        Record value = *i;
        Record *j = i;
        while (j > first && func_0026F060(comp, &value, j - 1)) {
            *j = j[-1];
            --j;
        }
        *j = value;
    }
}

extern "C" void func_005DFF68(Record *first, Record *last, Record *, int, Compare *comp) {
    insertion_sort(first, last, comp);
}
