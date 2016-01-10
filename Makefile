all: chant_node.c  
	gcc  chant_node.c -o chant_node -lpthread
	 

clean:
	rm -f *.o client server

	
.PHONY: all run
