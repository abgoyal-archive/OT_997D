
enum THE_LANGUAGES {
	EN, 
	ZH_CN,
	DE,
	ES,
	FR,
	PT,
	RU,
};


struct ml_strings {
	int lang;
	char name[16];
	char **header;
	char **menu;
};

