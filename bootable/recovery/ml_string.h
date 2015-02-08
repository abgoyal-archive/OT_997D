#define DEFAULT_LANG (EN)
#define PAGE_COUNT (3)

static const char *EN_main_header[] = {
	"Android system recovery utility",
	"",
	NULL
};

static const char *EN_main_menu[] = {
	"reboot system now",
	"apply update from sdcard",
	"apply update from cache",
	"wipe data/factory reset",
	"wipe cache partition",
#ifdef SUPPORT_DATA_BACKUP_RESTORE
	"backup user data",
	"restore user data",
#endif
	"⇦",
	NULL
};

static const char *EN_wipe_data_header[] = {
	"Confirm wipe of all user data?",
	"  THIS CAN NOT BE UNDONE.",
	"",
	NULL
};

static const char *EN_wipe_data_menu[] = {
	"No",
	"No",
	"No",
	"No",
	"No",
	"No",
	"No",
	"Yes -- delete all user data",
	"No",
	"No",
	"No",
	NULL
};

static const char *EN_updater_header[] = {
	"Choose a package to install:",
	"",
	NULL
};

static const char *EN_updater_menu[] = {
	NULL
};

static const char *ZH_CN_main_header[] = {
	"Android系统恢复工具",
	"",
	NULL
};

static const char *ZH_CN_main_menu[] = {
	"重新启动系统",
	"安装SD卡上的升级包",
	"安装缓存区内的升级包",
	"擦除用户数据/恢复出厂设置",
	"擦除缓存区",
#ifdef SUPPORT_DATA_BACKUP_RESTORE
	"备份用户数据",
	"应用上一次备份的用户数据",
#endif
	"⇦",
	NULL
};

static const char *ZH_CN_wipe_data_header[] = {
	"是否擦除所有用户数据?",
	"  注意：擦除之后，用户数据不能复原!",
	"",
	NULL
};

static const char *ZH_CN_wipe_data_menu[] = {
	"否",
	"否",
	"否",
	"否",
	"否",
	"否",
	"否",
	"是 -- 擦除所有用户数据",
	"否",
	"否",
	"否",
	NULL
};

static const char *ZH_CN_updater_header[] = {
	"选择升级包:",
	"",
	NULL
};

static const char *ZH_CN_updater_menu[] = {
	NULL
};

static const char *DE_main_header[] = {
	"Programm für die Android-Systemwiederherstellung",
	"",
	NULL
};

static const char *DE_main_menu[] = {
	"System jetzt neustarten",
	"Update von SD-Karte anwenden",
	"Update aus Cache anwenden",
	"Daten löschen/Standardeinstellungen wiederherstellen",
	"Cache-Partition löschen",
#ifdef SUPPORT_DATA_BACKUP_RESTORE
	"Benutzerdaten sichern",
	"Benutzerdaten wiederherstellen",
#endif
	"⇦",
	NULL
};

static const char *DE_wipe_data_header[] = {
	"Löschen aller Benutzerdaten bestätigen?",
	"DIESE AKTION KANN NICHT RÜCKGÄNGIG GEMACHT WERDEN.",
	"",
	NULL
};

static const char *DE_wipe_data_menu[] = {
	"Nein",
	"Nein",
	"Nein",
	"Nein",
	"Nein",
	"Nein",
	"Nein",
	"Ja - alle Benutzerdaten löschen",
	"Nein",
	"Nein",
	"Nein",
	NULL
};

static const char *DE_updater_header[] = {
	"Zu installierendes Paket auswählen:",
	"",
	NULL
};

static const char *DE_updater_menu[] = {
	NULL
};

static const char *ES_main_header[] = {
	"Utilidad de recuperación del sistema Android",
	"",
	NULL
};

static const char *ES_main_menu[] = {
	"Reiniciar el sistema ahora",
	"Aplicar actualización desde la tarjeta SD",
	"Aplicar actualización desde la caché",
	"Borrar datos/reiniciar valores de fábrica",
	"Borrar partición de la caché",
#ifdef SUPPORT_DATA_BACKUP_RESTORE
	"Realizar copia de seguridad de datos del usuario",
	"Restaurar datos del usuario",
#endif
	"⇦",
	NULL
};

static const char *ES_wipe_data_header[] = {
	"¿Desea confirmar laeliminación de todos los datos del usuario?",
	"  ESTA OPERACIÓN NO SE PUEDE DESHACER.",
	"",
	NULL
};

static const char *ES_wipe_data_menu[] = {
	"No",
	"No",
	"No",
	"No",
	"No",
	"No",
	"No",
	"Sí -- eliminar todos los datos del usuario.",
	"No",
	"No",
	"No",
	NULL
};

static const char *ES_updater_header[] = {
	"Elija el paquete para instalar:",
	"",
	NULL
};

static const char *ES_updater_menu[] = {
	NULL
};

static const char *FR_main_header[] = {
	"Système de restauration Android",
	"",
	NULL
};

static const char *FR_main_menu[] = {
	"Redémarrer le système maintenant",
	"Appliquer la mise à jour à partir de la sdcard",
	"Appliquer la mise à jour à partir de la cache",
	"Effacer les données/Restaurer les valeurs d’usine",
	"Effacer la partition cache",
#ifdef SUPPORT_DATA_BACKUP_RESTORE
	"Sauvegarder les données utilisateur",
	"Restaurer les données utilisateur",
#endif
	"⇦",
	NULL
};

static const char *FR_wipe_data_header[] = {
	"Confirmer l'effacement de toutes les données utilisateur?",
	"  ANNULATION IMPOSSIBLE.",
	"",
	NULL
};

static const char *FR_wipe_data_menu[] = {
	"Non",
	"Non",
	"Non",
	"Non",
	"Non",
	"Non",
	"Non",
	"Oui -- effacer toutes les données utilisateur",
	"Non",
	"Non",
	"Non",
	NULL
};

static const char *FR_updater_header[] = {
	"Choisissez un package à installer:",
	"",
	NULL
};

static const char *FR_updater_menu[] = {
	NULL
};

static const char *PT_main_header[] = {
	"Utilitário para recuperação do sistema Android",
	"",
	NULL
};

static const char *PT_main_menu[] = {
	"reiniciar o sistema agora",
	"aplicar actualização do sdcard",
	"aplicar actualização da cache",
	"apagar dados/reposição das configurações de fábrica",
	"apagar dados da partição da cache",
#ifdef SUPPORT_DATA_BACKUP_RESTORE
	"efectuar cópia de segurança dos dados do utilizador",
	"restaurar os dados do utilizador",
#endif
	"⇦",
	NULL
};

static const char *PT_wipe_data_header[] = {
	"Confirmar eliminação de todos os dados do utilizador?",
	"  ESTA ACÇÃO NÃO PODE SER ANULADA.",
	"",
	NULL
};

static const char *PT_wipe_data_menu[] = {
	"Não",
	"Não",
	"Não",
	"Não",
	"Não",
	"Não",
	"Não",
	"Sim -- eliminar todos os dados do utilizador",
	"Não",
	"Não",
	"Não",
	NULL
};

static const char *PT_updater_header[] = {
	"Seleccione um pacote para instalar:",
	"",
	NULL
};

static const char *PT_updater_menu[] = {
	NULL
};

static const char *RU_main_header[] = {
	"Программа восстановления системы Android",
	"",
	NULL
};

static const char *RU_main_menu[] = {
	"перезагрузить систему",
	"применить обновление с sd-карты",
	"применить обновление из кэша",
	"стереть данные/восстановить заводские установки",
	"стереть раздел кэша",
#ifdef SUPPORT_DATA_BACKUP_RESTORE
	"архивировать данные пользователя",
	"восстановить данные пользователя",
#endif
	"⇦",
	NULL
};

static const char *RU_wipe_data_header[] = {
	"Стереть все данные пользователя?",
	"  ЭТО ДЕЙСТВИЕ НЕВОЗМОЖНО ОТМЕНИТЬ.",
	"",
	NULL
};

static const char *RU_wipe_data_menu[] = {
	"Нет",
	"Нет",
	"Нет",
	"Нет",
	"Нет",
	"Нет",
	"Нет",
	"Да -- удалить все данные пользователя",
	"Нет",
	"Нет",
	"Нет",
	NULL
};

static const char *RU_updater_header[] = {
	"Выбор программы для установки:",
	"",
	NULL
};

static const char *RU_updater_menu[] = {
	NULL
};

const struct ml_strings g_ml_str[] = {
{
	.lang = EN,
	.name = "main",
	.header = EN_main_header,
	.menu = EN_main_menu,
},
{
	.lang = EN,
	.name = "wipe_data",
	.header = EN_wipe_data_header,
	.menu = EN_wipe_data_menu,
},
{
	.lang = EN,
	.name = "updater",
	.header = EN_updater_header,
	.menu = EN_updater_menu,
},
{
	.lang = ZH_CN,
	.name = "main",
	.header = ZH_CN_main_header,
	.menu = ZH_CN_main_menu,
},
{
	.lang = ZH_CN,
	.name = "wipe_data",
	.header = ZH_CN_wipe_data_header,
	.menu = ZH_CN_wipe_data_menu,
},
{
	.lang = ZH_CN,
	.name = "updater",
	.header = ZH_CN_updater_header,
	.menu = ZH_CN_updater_menu,
},
{
	.lang = DE,
	.name = "main",
	.header = DE_main_header,
	.menu = DE_main_menu,
},
{
	.lang = DE,
	.name = "wipe_data",
	.header = DE_wipe_data_header,
	.menu = DE_wipe_data_menu,
},
{
	.lang = DE,
	.name = "updater",
	.header = DE_updater_header,
	.menu = DE_updater_menu,
},
{
	.lang = ES,
	.name = "main",
	.header = ES_main_header,
	.menu = ES_main_menu,
},
{
	.lang = ES,
	.name = "wipe_data",
	.header = ES_wipe_data_header,
	.menu = ES_wipe_data_menu,
},
{
	.lang = ES,
	.name = "updater",
	.header = ES_updater_header,
	.menu = ES_updater_menu,
},
{
	.lang = FR,
	.name = "main",
	.header = FR_main_header,
	.menu = FR_main_menu,
},
{
	.lang = FR,
	.name = "wipe_data",
	.header = FR_wipe_data_header,
	.menu = FR_wipe_data_menu,
},
{
	.lang = FR,
	.name = "updater",
	.header = FR_updater_header,
	.menu = FR_updater_menu,
},
{
	.lang = PT,
	.name = "main",
	.header = PT_main_header,
	.menu = PT_main_menu,
},
{
	.lang = PT,
	.name = "wipe_data",
	.header = PT_wipe_data_header,
	.menu = PT_wipe_data_menu,
},
{
	.lang = PT,
	.name = "updater",
	.header = PT_updater_header,
	.menu = PT_updater_menu,
},
{
	.lang = RU,
	.name = "main",
	.header = RU_main_header,
	.menu = RU_main_menu,
},
{
	.lang = RU,
	.name = "wipe_data",
	.header = RU_wipe_data_header,
	.menu = RU_wipe_data_menu,
},
{
	.lang = RU,
	.name = "updater",
	.header = RU_updater_header,
	.menu = RU_updater_menu,
},

};

static const int supported_lang[] = {
	EN,
	ZH_CN,
	DE,
	ES,
	FR,
	PT,
	RU,

};
static const int supported_lang_cnt = (sizeof(supported_lang)/(sizeof(int)));

char *g_ml_str_hint[] = {
	"English",
	"中文",
	"Deutsch",
	"Español",
	"Français",
	"Português",
	"Русский",

	NULL
};

