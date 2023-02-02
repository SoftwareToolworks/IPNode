# Makefile for QPSK Demo Code

SRC=qpsk-demo.c costas_loop.c rrc_fir.c interpolatingSampleBuffer.c QPSKDemodulator.c symbolEvaluator.c
HEADER=qpsk-demo.h interp.h costas_loop.h rrc_fir.h

qpsk-demo: ${SRC} ${HEADER}
	gcc -std=c11 -g ${SRC} -DTEST_SCATTER -o qpsk-demo -Wall -lm

# generate scatter diagram PNG

test_scatter: qpsk-demo
	./qpsk-demo 2>scatter.txt
	DISPLAY="" octave-cli -qf --eval "load scatter.txt; plot(scatter(800:2000,1),scatter(800:2000,2),'.'); print('scatter.png','-dpng')"
  
