BINDIR=/usr/local/bin

replay183: replay183.o

install:
	test -d "$(DESTDIR)/$(BINDIR)"  || install -d -g $(INSTGROUP) -o root -m 755 $(DESTDIR)/$(BINDIR)
	install -g $(INSTGROUP) -o root -m 755 replay183 $(DESTDIR)/$(BINDIR)/replay183

clean:
	rm -f replay183 replay183.o
