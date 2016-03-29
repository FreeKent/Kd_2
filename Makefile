INC=./includes
SRC=./src
OUT=./bin


$(OUT)/lab: $(OUT)/main.o
	gcc -o $(OUT)/lab $(OUT)/main.o 

$(OUT)/main.o: $(SRC)/main.c $(INC)/kd_2.h
	gcc -I$(INC) -o $(OUT)/main.o -c $(SRC)/main.c

clean:
	rm $(OUT)/*.o $(OUT)/lab