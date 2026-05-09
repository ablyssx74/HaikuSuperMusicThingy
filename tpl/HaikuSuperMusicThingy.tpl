name			$(NAME)
version			$(VERSION)-1
architecture	$(ARCH)
summary 		"SomaFM music player"
description 	"HaikuSuperMusicThingy is a free streaming media client for SomaFM. Fast, light, and fun!"
packager		"ablyss <jb@epluribusunix.net>"
vendor			"Haiku Project"
licenses {
	"MIT"
}
copyrights {
	"$(YEAR) ablyss"
}
provides {
	$(NAME) = $(VERSION)-1
}
requires {
	haiku
	haiku_datatranslators
	nlohmann_json
	mpv
	openal
	curl
	
}	
urls {
	"https://github.com/ablyssx74/HaikuSuperMusicThingy"
}
source-urls {
# Download
	"https://github.com/ablyssx74/HaikuSuperMusicThingy/archive/refs/tags/v1.0.0.tar.gz"
}
