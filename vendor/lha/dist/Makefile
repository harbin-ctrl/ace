OS := $(shell uname | tr 'A-Z' 'a-z')
CPU := $(shell uname -m | tr 'A-Z' 'a-z' | sed -e 's/ower//')

LHa_4U4A-$(CPU)-$(OS).lha: lha LHa_4U4A.guide LHa_4U4A.guide.info
	lha ao5 $@ $^
	cat LHa_4U4A-CPU-OS.readme | sed -e "s/__ARC__/$(CPU)/" > \
	LHa_4U4A-$(CPU)-$(OS).readme
	mkdir -p tmp
	mv *$(CPU)-$(OS)* tmp

lha: ../src/lha
	cp $< $@

.PHONY: ../src/lha
../src/lha:
	$(MAKE) -C ..

.PHONY: clean
clean:
	$(RM) -rf lha *.lha tmp
