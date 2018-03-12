CFLAG=-g
OBJECT=hop.o

all:$(OBJECT)
	gcc $(OBJECT) -o hop -lpthread
clean:
	rm -r $(OBJECT)
	rm -r hop
run:$(OBJECT)
	./hop
