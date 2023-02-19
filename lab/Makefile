# Makefile for QPSK Demo Code

SRC=qpsk-demo.c costas_loop.c rrc_fir.c timing_error_detector.c constellation.c deque.c
HEADER=qpsk-demo.h costas_loop.h rrc_fir.h timing_error_detector.h constellation.h deque.h

qpsk-demo: ${SRC} ${HEADER}
	gcc -std=c11 -g ${SRC} -DFREQ_ERROR -DTEST_SCATTER -o qpsk-demo -Wall -lm
#	gcc -std=c11 -g ${SRC} -DTEST_SCATTER -o qpsk-demo -Wall -lm

# generate scatter diagram PNG

test_scatter: qpsk-demo
	./qpsk-demo 2>scatter.txt
	DISPLAY="" octave-cli -qf --eval "load scatter.txt; plot(scatter(800:2000,1),scatter(800:2000,2),'.'); print('scatter.png','-dpng')"
  
