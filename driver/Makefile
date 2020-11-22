PARTS = $(wildcard part*.sh)

all: list

list: $(PARTS) ./part-points.sh ./mkpartlist.sh
	./mkpartlist.sh > list

clean:
