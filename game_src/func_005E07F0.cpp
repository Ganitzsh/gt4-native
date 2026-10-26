/* Native stand-in for 005E07F0, the final insertion sort of the sort 005DFF68 stands in for (see
 * func_005DFF68.cpp): the same insertion sort, which finds the records already in order. */
struct Record { int w[11]; };
struct Compare;
extern "C" int func_0026F060(Compare *comp, const Record *a, const Record *b);

extern "C" void func_005E07F0(Record *first, Record *last, Compare *comp) {
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
