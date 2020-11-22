SUBDIRS = driver lib tiny src
PROXY = src/proxy

all:
	@for dir in $(SUBDIRS); do		\
		(cd $$dir; make all) || exit $$?;  \
	done

clean:
	@for dir in $(SUBDIRS); do		  \
		(cd $$dir; make clean) || exit $$?;  \
	done
	rm -f *.log
	rm -fR .proxy .noproxy

# Creates a tarball in ../proxylab-handin.tar that you can then
# hand in. DO NOT MODIFY THIS!
submit:
	make clean
	mkdir -p ~/submissions
	rm -f ~/submissions/$(USER)-proxylab.tar
	tar cvf ~/submissions/$(USER)-proxylab.tar src
