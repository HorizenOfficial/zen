ACLOCAL_AMFLAGS = -I build-aux/m4
SUBDIRS = src
if ENABLE_MAN
SUBDIRS += doc/man
endif
.PHONY: deploy FORCE

GZIP_ENV="-9n"

if BUILD_BITCOIN_LIBS
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libzcashconsensus.pc
endif

BITCOIND_BIN=$(top_builddir)/src/zend$(EXEEXT)
BITCOIN_QT_BIN=$(top_builddir)/src/qt/bitcoin-qt$(EXEEXT)
BITCOIN_CLI_BIN=$(top_builddir)/src/zen-cli$(EXEEXT)
BITCOIN_WIN_INSTALLER=$(PACKAGE)-$(PACKAGE_VERSION)-win$(WINDOWS_BITS)-setup$(EXEEXT)


##OSX_APP=Bitcoin-Qt.app
##OSX_DMG=Bitcoin-Core.dmg
##OSX_BACKGROUND_IMAGE=background.tiff
##OSX_DEPLOY_SCRIPT=$(top_srcdir)/contrib/macdeploy/macdeployqtplus
##OSX_FANCY_PLIST=$(top_srcdir)/contrib/macdeploy/fancy.plist
##OSX_BASE_LPROJ_DIR=$(top_srcdir)/contrib/macdeploy/Base.lproj/InfoPlist.strings
##OSX_INSTALLER_ICONS=$(top_srcdir)/src/qt/res/icons/bitcoin.icns
##OSX_PLIST=$(top_srcdir)/share/qt/Info.plist #not installed
##OSX_QT_TRANSLATIONS = da,de,es,hu,ru,uk,zh_CN,zh_TW

DIST_DOCS = $(wildcard doc/*.md) $(wildcard doc/release-notes/*.md)

BIN_CHECKS=$(top_srcdir)/contrib/devtools/symbol-check.py \
           $(top_srcdir)/contrib/devtools/security-check.py

WINDOWS_PACKAGING = $(top_srcdir)/share/pixmaps/bitcoin.ico \
  $(top_srcdir)/share/pixmaps/nsis-header.bmp \
  $(top_srcdir)/share/pixmaps/nsis-wizard.bmp

##OSX_PACKAGING = $(OSX_DEPLOY_SCRIPT) $(OSX_FANCY_PLIST) $(OSX_INSTALLER_ICONS) $(OSX_BASE_LPROJ_DIR) \
##  $(top_srcdir)/contrib/macdeploy/$(OSX_BACKGROUND_IMAGE) \
##  $(top_srcdir)/contrib/macdeploy/DS_Store \
##  $(top_srcdir)/contrib/macdeploy/detached-sig-apply.sh \
##  $(top_srcdir)/contrib/macdeploy/detached-sig-create.sh

COVERAGE_INFO = baseline_filtered_combined.info baseline.info \
  leveldb_baseline.info test_bitcoin_filtered.info total_coverage.info \
  baseline_filtered.info \
  leveldb_baseline_filtered.info test_bitcoin_coverage.info test_bitcoin.info \
  zen-gtest.info zen-gtest_filtered.info zen-gtest_coverage.info

dist-hook:
	-$(MAKE) -C $(top_distdir)/src/leveldb clean
	-$(MAKE) -C $(top_distdir)/src/secp256k1 distclean
	-$(GIT) archive --format=tar HEAD -- src/clientversion.cpp | $(AMTAR) -C $(top_distdir) -xf -

distcheck-hook:
	$(MKDIR_P) $(top_distdir)/_build/src/leveldb
	cp -rf $(top_srcdir)/src/leveldb/* $(top_distdir)/_build/src/leveldb/
	-$(MAKE) -C $(top_distdir)/_build/src/leveldb clean

distcleancheck:
	@:

$(BITCOIN_WIN_INSTALLER): all-recursive
	$(MKDIR_P) $(top_builddir)/release
	STRIPPROG="$(STRIP)" $(INSTALL_STRIP_PROGRAM) $(BITCOIND_BIN) $(top_builddir)/release
	STRIPPROG="$(STRIP)" $(INSTALL_STRIP_PROGRAM) $(BITCOIN_CLI_BIN) $(top_builddir)/release
#	STRIPPROG="$(STRIP)" $(INSTALL_STRIP_PROGRAM) $(WALLET_UTILITY_BIN) $(top_builddir)/release
	@test -f $(MAKENSIS) && $(MAKENSIS) -V2 $(top_builddir)/share/setup.nsi || \
	  echo error: could not build $@
	@echo built $@

$(if $(findstring src/,$(MAKECMDGOALS)),$(MAKECMDGOALS), none): FORCE
	$(MAKE) -C src $(patsubst src/%,%,$@)

##$(OSX_APP)/Contents/PkgInfo:
##	$(MKDIR_P) $(@D)
##	@echo "APPL????" > $@
##
##$(OSX_APP)/Contents/Resources/empty.lproj:
##	$(MKDIR_P) $(@D)
##	@touch $@
##
##$(OSX_APP)/Contents/Info.plist: $(OSX_PLIST)
##	$(MKDIR_P) $(@D)
##	$(INSTALL_DATA) $< $@
##
##$(OSX_APP)/Contents/Resources/bitcoin.icns: $(OSX_INSTALLER_ICONS)
##	$(MKDIR_P) $(@D)
##	$(INSTALL_DATA) $< $@
##
##$(OSX_APP)/Contents/MacOS/Bitcoin-Qt: $(BITCOIN_QT_BIN)
##	$(MKDIR_P) $(@D)
##	STRIPPROG="$(STRIP)" $(INSTALL_STRIP_PROGRAM)  $< $@
##
##$(OSX_APP)/Contents/Resources/Base.lproj/InfoPlist.strings: $(OSX_BASE_LPROJ_DIR)
##	$(MKDIR_P) $(@D)
##	$(INSTALL_DATA) $< $@
##
##OSX_APP_BUILT=$(OSX_APP)/Contents/PkgInfo $(OSX_APP)/Contents/Resources/empty.lproj \
##  $(OSX_APP)/Contents/Resources/bitcoin.icns $(OSX_APP)/Contents/Info.plist \
##  $(OSX_APP)/Contents/MacOS/Bitcoin-Qt $(OSX_APP)/Contents/Resources/Base.lproj/InfoPlist.strings
##
##if BUILD_DARWIN
##$(OSX_DMG): $(OSX_APP_BUILT) $(OSX_PACKAGING)
##	$(OSX_DEPLOY_SCRIPT) $(OSX_APP) -add-qt-tr $(OSX_QT_TRANSLATIONS) -translations-dir=$(QT_TRANSLATION_DIR) -dmg -fancy $(OSX_FANCY_PLIST) -verbose 2
##
##deploydir: $(OSX_DMG)
##else
##APP_DIST_DIR=$(top_builddir)/dist
##APP_DIST_EXTRAS=$(APP_DIST_DIR)/.background/$(OSX_BACKGROUND_IMAGE) $(APP_DIST_DIR)/.DS_Store $(APP_DIST_DIR)/Applications
##
##$(APP_DIST_DIR)/Applications:
##	@rm -f $@
##	@cd $(@D); $(LN_S) /Applications $(@F)
##
##$(APP_DIST_EXTRAS): $(APP_DIST_DIR)/$(OSX_APP)/Contents/MacOS/Bitcoin-Qt
##
##$(OSX_DMG): $(APP_DIST_EXTRAS)
##	$(GENISOIMAGE) -no-cache-inodes -D -l -probe -V "Bitcoin-Core" -no-pad -r -apple -o $@ dist
##
##$(APP_DIST_DIR)/.background/$(OSX_BACKGROUND_IMAGE): contrib/macdeploy/$(OSX_BACKGROUND_IMAGE)
##	$(MKDIR_P) $(@D)
##	$(INSTALL) $< $@
##$(APP_DIST_DIR)/.DS_Store: contrib/macdeploy/DS_Store
##	$(INSTALL) $< $@
##
##$(APP_DIST_DIR)/$(OSX_APP)/Contents/MacOS/Bitcoin-Qt: $(OSX_APP_BUILT) $(OSX_PACKAGING)
##	INSTALLNAMETOOL=$(INSTALLNAMETOOL)  OTOOL=$(OTOOL) STRIP=$(STRIP) $(OSX_DEPLOY_SCRIPT) $(OSX_APP) -translations-dir=$(QT_TRANSLATION_DIR) -add-qt-tr $(OSX_QT_TRANSLATIONS) -verbose 2
##
##deploydir: $(APP_DIST_EXTRAS)
##endif
##
##if TARGET_DARWIN
##appbundle: $(OSX_APP_BUILT)
##deploy: $(OSX_DMG)
##endif

if TARGET_WINDOWS
deploy: $(BITCOIN_WIN_INSTALLER)
endif

$(BITCOIND_BIN): FORCE
	$(MAKE) -C src $(@F)

$(BITCOIN_CLI_BIN): FORCE
	$(MAKE) -C src $(@F)

#$(WALLET_UTILITY_BIN): FORCE
#	$(MAKE) -C src $(@F)

if USE_LCOV

baseline.info:
	$(LCOV) -c -i -d $(abs_builddir)/src -o $@

baseline_filtered.info: baseline.info
	$(LCOV) -r $< "/usr/include/*" \
                      "$(abs_builddir)/depends/x86_64-unknown-linux-gnu/include/*.h" \
                      "$(abs_builddir)/depends/x86_64-unknown-linux-gnu/include/boost/*" \
                      "$(abs_builddir)/depends/x86_64-unknown-linux-gnu/include/gmock/*" \
                      "$(abs_builddir)/depends/x86_64-unknown-linux-gnu/include/gtest/*" \
                      "$(abs_builddir)/depends/x86_64-linux-gnu/include/*.h" \
                      "$(abs_builddir)/depends/x86_64-linux-gnu/include/boost/*" \
                      "$(abs_builddir)/depends/x86_64-linux-gnu/include/gmock/*" \
                      "$(abs_builddir)/depends/x86_64-linux-gnu/include/gtest/*" \
                      "$(abs_builddir)/src/gtest/*" \
                      "$(abs_builddir)/src/test/*" \
                      "$(abs_builddir)/src/wallet/gtest/*" \
                      "$(abs_builddir)/src/wallet/test/*" \
                      -o $@

leveldb_baseline.info: baseline_filtered.info
	$(LCOV) -c -i -d $(abs_builddir)/src/leveldb -b $(abs_builddir)/src/leveldb -o $@

leveldb_baseline_filtered.info: leveldb_baseline.info
	$(LCOV) -r $< "/usr/include/*" \
                      "$(abs_builddir)/depends/x86_64-unknown-linux-gnu/include/*.h" \
                      "$(abs_builddir)/depends/x86_64-unknown-linux-gnu/include/boost/*" \
                      "$(abs_builddir)/depends/x86_64-unknown-linux-gnu/include/gmock/*" \
                      "$(abs_builddir)/depends/x86_64-unknown-linux-gnu/include/gtest/*" \
                      "$(abs_builddir)/depends/x86_64-linux-gnu/include/*.h" \
                      "$(abs_builddir)/depends/x86_64-linux-gnu/include/boost/*" \
                      "$(abs_builddir)/depends/x86_64-linux-gnu/include/gmock/*" \
                      "$(abs_builddir)/depends/x86_64-linux-gnu/include/gtest/*" \
                      "$(abs_builddir)/src/gtest/*" \
                      "$(abs_builddir)/src/test/*" \
                      "$(abs_builddir)/src/wallet/gtest/*" \
                      "$(abs_builddir)/src/wallet/test/*" \
                      -o $@

baseline_filtered_combined.info: leveldb_baseline_filtered.info baseline_filtered.info
	$(LCOV) -a leveldb_baseline_filtered.info -a baseline_filtered.info -o $@

test_bitcoin.info: baseline_filtered_combined.info
	$(MAKE) -C src/ bitcoin_test_check
	$(LCOV) -c -d $(abs_builddir)/src -t test_bitcoin -o $@
	$(LCOV) -z -d $(abs_builddir)/src
	$(LCOV) -z -d $(abs_builddir)/src/leveldb

test_bitcoin_filtered.info: test_bitcoin.info
	$(LCOV) -r $< "/usr/include/*" \
                      "$(abs_builddir)/depends/x86_64-unknown-linux-gnu/include/*.h" \
                      "$(abs_builddir)/depends/x86_64-unknown-linux-gnu/include/boost/*" \
                      "$(abs_builddir)/depends/x86_64-unknown-linux-gnu/include/gmock/*" \
                      "$(abs_builddir)/depends/x86_64-unknown-linux-gnu/include/gtest/*" \
                      "$(abs_builddir)/depends/x86_64-linux-gnu/include/*.h" \
                      "$(abs_builddir)/depends/x86_64-linux-gnu/include/boost/*" \
                      "$(abs_builddir)/depends/x86_64-linux-gnu/include/gmock/*" \
                      "$(abs_builddir)/depends/x86_64-linux-gnu/include/gtest/*" \
                      "$(abs_builddir)/src/gtest/*" \
                      "$(abs_builddir)/src/test/*" \
                      "$(abs_builddir)/src/wallet/gtest/*" \
                      "$(abs_builddir)/src/wallet/test/*" \
                      -o $@

zen-gtest.info: baseline_filtered_combined.info
	$(MAKE) -C src/ zen-gtest_check
	$(LCOV) -c -d $(abs_builddir)/src -t zen-gtest -o $@
	$(LCOV) -z -d $(abs_builddir)/src
	$(LCOV) -z -d $(abs_builddir)/src/leveldb

zen-gtest_filtered.info: zen-gtest.info
	$(LCOV) -r $< "/usr/include/*" \
                      "$(abs_builddir)/depends/x86_64-unknown-linux-gnu/include/*.h" \
                      "$(abs_builddir)/depends/x86_64-unknown-linux-gnu/include/boost/*" \
                      "$(abs_builddir)/depends/x86_64-unknown-linux-gnu/include/gmock/*" \
                      "$(abs_builddir)/depends/x86_64-unknown-linux-gnu/include/gtest/*" \
                      "$(abs_builddir)/depends/x86_64-linux-gnu/include/*.h" \
                      "$(abs_builddir)/depends/x86_64-linux-gnu/include/boost/*" \
                      "$(abs_builddir)/depends/x86_64-linux-gnu/include/gmock/*" \
                      "$(abs_builddir)/depends/x86_64-linux-gnu/include/gtest/*" \
                      "$(abs_builddir)/src/gtest/*" \
                      "$(abs_builddir)/src/test/*" \
                      "$(abs_builddir)/src/wallet/gtest/*" \
                      "$(abs_builddir)/src/wallet/test/*" \
                      -o $@

test_bitcoin_coverage.info: baseline_filtered_combined.info test_bitcoin_filtered.info
	$(LCOV) -a baseline_filtered.info -a leveldb_baseline_filtered.info -a test_bitcoin_filtered.info -o $@

zen-gtest_coverage.info: baseline_filtered_combined.info zen-gtest_filtered.info
	$(LCOV) -a baseline_filtered.info -a leveldb_baseline_filtered.info -a zen-gtest_filtered.info -o $@

total_coverage.info:  baseline_filtered_combined.info test_bitcoin_filtered.info zen-gtest_filtered.info
	$(LCOV) -a baseline_filtered.info -a leveldb_baseline_filtered.info -a test_bitcoin_filtered.info -a zen-gtest_filtered.info -o $@ | $(GREP) "\%" | $(AWK) '{ print substr($$3,2,50) "/" $$5 }' > coverage_percent.txt

test_bitcoin.coverage/.dirstamp:  test_bitcoin_coverage.info
	$(GENHTML) -s $< -o $(@D)
	@touch $@

zen-gtest.coverage/.dirstamp:  zen-gtest_coverage.info
	$(GENHTML) -s $< -o $(@D)
	@touch $@

cov-zen: zen-gtest.coverage/.dirstamp

total.coverage/.dirstamp: total_coverage.info
	$(GENHTML) -s $< -o $(@D)
	@touch $@

cov: test_bitcoin.coverage/.dirstamp cov-zen total.coverage/.dirstamp

COMMIT = `git log -1 --format="%H"`

cov_ci: test_bitcoin.coverage/.dirstamp cov-zen total.coverage/.dirstamp
	bash <(curl -Ls https://coverage.codacy.com/get.sh) report --partial -l CPP --commit-uuid ${COMMIT} \
    --project-token ${CODACY_PROJECT_TOKEN} -r total_coverage.info 
	bash <(curl -Ls https://coverage.codacy.com/get.sh) report --partial -l C   --commit-uuid ${COMMIT} \
    --project-token ${CODACY_PROJECT_TOKEN} -r total_coverage.info

endif

dist_bin_SCRIPTS = zcutil/fetch-params.sh
dist_noinst_SCRIPTS = autogen.sh zcutil/build-debian-package.sh zcutil/build.sh

EXTRA_DIST = $(top_srcdir)/share/genbuild.sh qa/pull-tester/rpc-tests.sh qa/pull-tester/run-bitcoin-cli qa/rpc-tests qa/zen $(DIST_DOCS) $(BIN_CHECKS)

install-exec-hook:
	mv $(DESTDIR)$(bindir)/fetch-params.sh $(DESTDIR)$(bindir)/zen-fetch-params

CLEANFILES = $(OSX_DMG) $(BITCOIN_WIN_INSTALLER)

DISTCHECK_CONFIGURE_FLAGS = --enable-man

clean-local:
	rm -rf test_bitcoin.coverage/ zen-gtest.coverage/ total.coverage/ $(OSX_APP)
