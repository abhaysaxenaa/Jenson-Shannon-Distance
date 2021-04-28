typedef struct {
    size_t length;
    size_t used;
    char *data;
} strbuf_t;

int strbuf_init(strbuf_t *L, size_t length){
    L->data = malloc(sizeof(char) * (length));
    if (!L->data) return 1;

    L->length = length;
    L->used = 0;
    L->data[L->used] = '\0';

    return 0;
}

void strbuf_destroy(strbuf_t *L){
    free(L->data);
}

int strbuf_append(strbuf_t *L, char item){
    //using (used + 1) here to account for the presence of the null terminator
    if ((L->used + 1) == L->length) {
        //double the array length
        size_t size = L->length * 2;
        char *p = realloc(L->data, sizeof(char) * size);
        if (!p) return 1;

        L->data = p;
        L->length = size;

    }

    //append the item to the list, & move null byte
    L->data[L->used] = item;
    L->data[L->used + 1] = '\0';
    ++L->used;

    return 0;
}
