#ifndef QUEUE_H_
#define QUEUE_H_

struct entry {
  int id;
  char *filename;
  int time;
  int size;
  char *contents;
};

struct entry* createList(int size);
int numberOfEntries();
int insert(struct entry add_item, struct entry *n);
int delete(struct entry *queue_array);
void display(struct entry *queue_array);

#endif
