#include "Lang.h"

#include <atomic>
#include <unordered_map>

namespace Lang
{
	namespace
	{
		// English, French, German, Italian, Spanish, Polish, Russian, Japanese, Chinese (traditional)
		using Entry = std::array<const char*, kCount>;

		constexpr std::array<const char*, kCount> kNativeNames{ "English", "Français", "Deutsch", "Italiano", "Español", "Polski",
			"Русский", "日本語", "繁體中文" };
		constexpr std::array<std::string_view, kCount> kGameNames{ "ENGLISH", "FRENCH", "GERMAN", "ITALIAN", "SPANISH", "POLISH",
			"RUSSIAN", "JAPANESE", "CHINESE" };

		constexpr Entry kTexts[] = {
			// menu entries
			{ "Item Inspection", "Inspection d'objets", "Gegenstandsansicht", "Ispezione oggetti", "Inspección de objetos",
				"Oglądanie przedmiotów", "Осмотр предметов", "アイテム観察", "物品檢視" },
			{ "Settings", "Paramètres", "Einstellungen", "Impostazioni", "Ajustes", "Ustawienia", "Настройки", "設定", "設定" },
			{ "Languages", "Langues", "Sprachen", "Lingue", "Idiomas", "Języki", "Языки", "言語", "語言" },

			// General
			{ "General", "Général", "Allgemein", "Generale", "General", "Ogólne", "Общие", "一般", "一般" },
			{ "Enabled", "Activé", "Aktiviert", "Attivo", "Activado", "Włączone", "Включено", "有効", "啟用" },
			{ "Items you take from the world go to your hand first.", "Les objets que vous ramassez passent d'abord par votre main.",
				"Gegenstände, die du aufhebst, landen zuerst in deiner Hand.", "Gli oggetti che raccogli finiscono prima nella tua mano.",
				"Los objetos que recoges van primero a tu mano.", "Podnoszone przedmioty trafiają najpierw do twojej ręki.",
				"Подобранные предметы сначала оказываются у вас в руке.", "拾ったアイテムはまず手に取って見ることができます。",
				"從世界中拾取的物品會先拿在手上。" },
			{ "Show it every time", "Montrer à chaque fois", "Jedes Mal zeigen", "Mostra ogni volta", "Mostrar siempre",
				"Pokazuj za każdym razem", "Показывать каждый раз", "毎回表示する", "每次都顯示" },
			{ "Off: you look at each kind of item only the first time you take it in this playthrough.\n"
			  "After that it goes straight into the inventory like in the normal game.\n"
			  "On: every item you take goes to your hand first.",
				"Désactivé : chaque type d'objet n'est montré que la première fois que vous le prenez dans cette partie.\n"
				"Ensuite, il va directement dans l'inventaire comme dans le jeu normal.\n"
				"Activé : chaque objet que vous prenez passe d'abord par votre main.",
				"Aus: Jede Art von Gegenstand siehst du nur beim ersten Aufheben in diesem Spieldurchlauf an.\n"
				"Danach kommt sie wie im normalen Spiel direkt ins Inventar.\n"
				"An: Jeder Gegenstand landet zuerst in deiner Hand.",
				"Disattivato: ogni tipo di oggetto viene mostrato solo la prima volta che lo prendi in questa partita.\n"
				"Dopo va direttamente nell'inventario come nel gioco normale.\n"
				"Attivato: ogni oggetto che prendi finisce prima nella tua mano.",
				"Desactivado: cada tipo de objeto solo se muestra la primera vez que lo coges en esta partida.\n"
				"Después va directamente al inventario, como en el juego normal.\n"
				"Activado: cada objeto que coges va primero a tu mano.",
				"Wyłączone: każdy rodzaj przedmiotu oglądasz tylko przy pierwszym podniesieniu w tej rozgrywce.\n"
				"Potem trafia od razu do ekwipunku, jak w zwykłej grze.\n"
				"Włączone: każdy podniesiony przedmiot trafia najpierw do ręki.",
				"Выкл.: каждый вид предмета показывается только при первом подборе в этом прохождении.\n"
				"Потом он сразу попадает в инвентарь, как в обычной игре.\n"
				"Вкл.: каждый подобранный предмет сначала оказывается в руке.",
				"オフ：この周回で初めて拾った種類のアイテムだけを手に取って見ます。\n"
				"2回目以降は通常どおり直接インベントリに入ります。\n"
				"オン：拾ったアイテムは毎回まず手に取ります。",
				"關閉：每種物品只有在本次遊玩中第一次拾取時才會拿在手上。\n"
				"之後會像原版一樣直接放進物品欄。\n"
				"開啟：每次拾取的物品都會先拿在手上。" },
			{ "Already looked at in this playthrough: {} kinds of items", "Déjà examinés dans cette partie : {} types d'objets",
				"In diesem Durchlauf schon angesehen: {} Arten von Gegenständen", "Già esaminati in questa partita: {} tipi di oggetti",
				"Ya examinados en esta partida: {} tipos de objetos", "Już obejrzane w tej rozgrywce rodzaje przedmiotów: {}",
				"Уже осмотрено в этом прохождении видов предметов: {}", "この周回で観察済み：{} 種類", "本次遊玩已檢視：{} 種物品" },
			{ "Forget them", "Les oublier", "Vergessen", "Dimenticali", "Olvidarlos", "Zapomnij", "Забыть", "リセット", "全部忘記" },
			{ "Every kind of item shows up in your hand again the next time you take it.",
				"Chaque type d'objet repassera par votre main la prochaine fois que vous le prendrez.",
				"Jede Art von Gegenstand landet beim nächsten Aufheben wieder in deiner Hand.",
				"Ogni tipo di oggetto tornerà nella tua mano la prossima volta che lo prendi.",
				"Cada tipo de objeto volverá a tu mano la próxima vez que lo cojas.",
				"Każdy rodzaj przedmiotu znów trafi do ręki przy następnym podniesieniu.",
				"Каждый вид предмета снова окажется в руке при следующем подборе.",
				"次に拾ったとき、すべての種類のアイテムを再び手に取ります。", "下次拾取時，每種物品都會再次拿在手上。" },
			{ "Scroll up on an item in the inventory to take it in your hand",
				"Faire défiler vers le haut sur un objet de l'inventaire pour le prendre en main",
				"Im Inventar auf einem Gegenstand nach oben scrollen, um ihn in die Hand zu nehmen",
				"Scorri verso l'alto su un oggetto dell'inventario per prenderlo in mano",
				"Desplaza hacia arriba sobre un objeto del inventario para cogerlo con la mano",
				"Przewiń w górę na przedmiocie w ekwipunku, aby wziąć go do ręki", "Прокрутка вверх на предмете в инвентаре берёт его в руку",
				"インベントリでアイテム上でホイールを上に回すと手に取る", "在物品欄中對物品向上捲動即可拿在手上" },
			{ "Instead of the zoomed preview, the inventory closes and you hold the item.\nPutting it away opens the inventory again.",
				"Au lieu de l'aperçu zoomé, l'inventaire se ferme et vous tenez l'objet.\nLe ranger rouvre l'inventaire.",
				"Statt der vergrößerten Vorschau schließt sich das Inventar und du hältst den Gegenstand.\nWegstecken öffnet das Inventar wieder.",
				"Invece dell'anteprima ingrandita, l'inventario si chiude e tieni l'oggetto in mano.\nRiporlo riapre l'inventario.",
				"En lugar de la vista ampliada, el inventario se cierra y sostienes el objeto.\nAl guardarlo, el inventario se vuelve a abrir.",
				"Zamiast powiększonego podglądu ekwipunek się zamyka, a ty trzymasz przedmiot.\nOdłożenie go ponownie otwiera ekwipunek.",
				"Вместо увеличенного просмотра инвентарь закрывается, и вы держите предмет.\nКогда вы его убираете, инвентарь открывается снова.",
				"拡大プレビューの代わりにインベントリが閉じ、アイテムを手に持ちます。\nしまうとインベントリが再び開きます。",
				"物品欄會關閉並改為拿著物品，而不是放大預覽。\n收起後物品欄會再次開啟。" },
			{ "Look at items you steal too", "Examiner aussi les objets volés", "Auch gestohlene Gegenstände ansehen",
				"Esamina anche gli oggetti rubati", "Examinar también los objetos robados", "Oglądaj też kradzione przedmioty",
				"Осматривать и краденые предметы", "盗むアイテムも観察する", "偷竊的物品也要檢視" },
			{ "On: stolen items go to your hand first as well. It only counts as stealing when you put the item\n"
			  "in your backpack; putting it back is no crime.\nOff: stealing works like in the normal game.",
				"Activé : les objets volés passent aussi d'abord par votre main. Le vol n'a lieu que lorsque vous mettez\n"
				"l'objet dans votre sac ; le reposer n'est pas un crime.\nDésactivé : le vol fonctionne comme dans le jeu normal.",
				"An: Auch gestohlene Gegenstände landen zuerst in deiner Hand. Als Diebstahl zählt es erst, wenn du den\n"
				"Gegenstand in den Rucksack steckst; Zurücklegen ist kein Verbrechen.\nAus: Stehlen funktioniert wie im normalen Spiel.",
				"Attivato: anche gli oggetti rubati finiscono prima nella tua mano. È furto solo quando metti l'oggetto\n"
				"nello zaino; rimetterlo a posto non è un crimine.\nDisattivato: il furto funziona come nel gioco normale.",
				"Activado: los objetos robados también van primero a tu mano. Solo cuenta como robo cuando guardas\n"
				"el objeto en la mochila; devolverlo no es delito.\nDesactivado: robar funciona como en el juego normal.",
				"Włączone: kradzione przedmioty też trafiają najpierw do ręki. Kradzież liczy się dopiero, gdy włożysz\n"
				"przedmiot do plecaka; odłożenie go nie jest przestępstwem.\nWyłączone: kradzież działa jak w zwykłej grze.",
				"Вкл.: краденые предметы тоже сначала оказываются в руке. Кражей это считается, только когда вы кладёте\n"
				"предмет в рюкзак; вернуть его на место — не преступление.\nВыкл.: кража работает как в обычной игре.",
				"オン：盗むアイテムもまず手に取ります。窃盗になるのはアイテムを\n"
				"バックパックに入れたときだけで、元に戻せば罪になりません。\nオフ：窃盗は通常どおりです。",
				"開啟：偷竊的物品也會先拿在手上。只有把物品放進背包時才算偷竊；\n放回原處不算犯罪。\n關閉：偷竊與原版相同。" },
			{ "Telekinesis", "Télékinésie", "Telekinese", "Telecinesi", "Telequinesis", "Telekineza", "Телекинез", "テレキネシス", "念力" },
			{ "The item floats over your hand with the Telekinesis spell's hand effect,\nand your fingers move while you turn it.",
				"L'objet flotte au-dessus de votre main avec l'effet du sort Télékinésie,\net vos doigts bougent pendant que vous le tournez.",
				"Der Gegenstand schwebt mit dem Handeffekt des Telekinese-Zaubers über deiner Hand,\nund deine Finger bewegen sich, während du ihn drehst.",
				"L'oggetto fluttua sopra la tua mano con l'effetto dell'incantesimo Telecinesi,\ne le dita si muovono mentre lo ruoti.",
				"El objeto flota sobre tu mano con el efecto del hechizo Telequinesis,\ny tus dedos se mueven mientras lo giras.",
				"Przedmiot unosi się nad dłonią z efektem czaru Telekineza,\na palce poruszają się, gdy go obracasz.",
				"Предмет парит над ладонью с эффектом заклинания «Телекинез»,\nа пальцы шевелятся, когда вы его вращаете.",
				"アイテムがテレキネシス呪文のエフェクトと共に手の上に浮かび、\n回すと指が動きます。",
				"物品會帶著念力法術的手部特效浮在手上，\n旋轉時手指也會跟著動。" },
			{ "Effect size", "Taille de l'effet", "Effektgröße", "Dimensione effetto", "Tamaño del efecto", "Rozmiar efektu",
				"Размер эффекта", "エフェクトの大きさ", "特效大小" },
			{ "Size of the telekinesis effect on the hand. 1 = like the spell.",
				"Taille de l'effet de télékinésie sur la main. 1 = comme le sort.", "Größe des Telekinese-Effekts an der Hand. 1 = wie beim Zauber.",
				"Dimensione dell'effetto telecinesi sulla mano. 1 = come l'incantesimo.",
				"Tamaño del efecto de telequinesis en la mano. 1 = como el hechizo.", "Rozmiar efektu telekinezy na dłoni. 1 = jak w czarze.",
				"Размер эффекта телекинеза на руке. 1 = как у заклинания.", "手のテレキネシスエフェクトの大きさ。1 = 呪文と同じ。",
				"手上念力特效的大小。1 = 與法術相同。" },
			{ "Hand light", "Lumière de la main", "Handlicht", "Luce della mano", "Luz de la mano", "Światło dłoni", "Свет руки", "手の光",
				"手部光芒" },
			{ "The spell's orange light around the hand.", "La lumière orange du sort autour de la main.",
				"Das orange Licht des Zaubers um die Hand.", "La luce arancione dell'incantesimo attorno alla mano.",
				"La luz naranja del hechizo alrededor de la mano.", "Pomarańczowe światło czaru wokół dłoni.",
				"Оранжевый свет заклинания вокруг руки.", "手の周りの呪文のオレンジ色の光。", "手部周圍法術的橙色光芒。" },
			{ "Sound", "Son", "Ton", "Suono", "Sonido", "Dźwięk", "Звук", "サウンド", "音效" },
			{ "The telekinesis grab sound when you pick the item up, put it in the backpack or put it back.",
				"Le son de saisie de la télékinésie quand vous prenez l'objet, le mettez dans le sac ou le reposez.",
				"Der Greif-Ton der Telekinese beim Aufheben, Einstecken oder Zurücklegen.",
				"Il suono di presa della telecinesi quando prendi l'oggetto, lo metti nello zaino o lo rimetti a posto.",
				"El sonido de agarre de la telequinesis al coger el objeto, guardarlo en la mochila o devolverlo.",
				"Dźwięk chwytu telekinezy przy podnoszeniu, wkładaniu do plecaka i odkładaniu.",
				"Звук захвата телекинеза, когда вы берёте предмет, кладёте в рюкзак или возвращаете.",
				"アイテムを拾う・しまう・戻すときのテレキネシスのつかむ音。", "拾取物品、放進背包或放回原處時的念力抓取音效。" },
			{ "Show the key hint", "Afficher l'aide des touches", "Tastenhinweis anzeigen", "Mostra suggerimento tasti",
				"Mostrar ayuda de teclas", "Pokaż podpowiedź klawiszy", "Показывать подсказку клавиш", "キーのヒントを表示", "顯示按鍵提示" },
			{ "The keys to put the item away or back, on screen while you hold it.",
				"Les touches pour ranger ou reposer l'objet, affichées tant que vous le tenez.",
				"Die Tasten zum Einstecken oder Zurücklegen, solange du den Gegenstand hältst.",
				"I tasti per riporre o rimettere a posto l'oggetto, visibili mentre lo tieni.",
				"Las teclas para guardar o devolver el objeto, en pantalla mientras lo sostienes.",
				"Klawisze do schowania lub odłożenia przedmiotu, widoczne, gdy go trzymasz.",
				"Клавиши, чтобы убрать или вернуть предмет, видны, пока вы его держите.",
				"アイテムを持っている間、しまう・戻すキーを画面に表示します。", "拿著物品時在畫面上顯示收起或放回的按鍵。" },
			{ "Everyone waits for you", "Tout le monde vous attend", "Alle warten auf dich", "Tutti ti aspettano", "Todos te esperan",
				"Wszyscy na ciebie czekają", "Все ждут вас", "周囲は待ってくれる", "所有人都會等你" },
			{ "While you hold an item nobody talks to you, attacks you or can hurt you:\npeople pause what they are doing until you put the item away.",
				"Pendant que vous tenez un objet, personne ne vous parle, ne vous attaque ni ne peut vous blesser :\nles gens s'interrompent jusqu'à ce que vous rangiez l'objet.",
				"Solange du einen Gegenstand hältst, spricht dich niemand an, greift dich an oder kann dich verletzen:\nalle halten inne, bis du ihn wegsteckst.",
				"Mentre tieni un oggetto nessuno ti parla, ti attacca o può ferirti:\nle persone si fermano finché non riponi l'oggetto.",
				"Mientras sostienes un objeto nadie te habla, te ataca ni puede herirte:\nla gente se detiene hasta que guardas el objeto.",
				"Gdy trzymasz przedmiot, nikt się do ciebie nie odzywa, nie atakuje cię ani nie może zranić:\nwszyscy czekają, aż go odłożysz.",
				"Пока вы держите предмет, никто с вами не заговорит, не нападёт и не сможет ранить:\nвсе замирают, пока вы его не уберёте.",
				"アイテムを持っている間は誰も話しかけたり攻撃したりせず、ダメージも受けません：\nアイテムをしまうまで周囲は動きを止めます。",
				"拿著物品時，沒有人會跟你說話、攻擊你或傷害你：\n大家會暫停動作，直到你收起物品。" },
			{ "Fade to black when the camera switches", "Fondu au noir lors du changement de caméra", "Abblende beim Kamerawechsel",
				"Dissolvenza al nero al cambio di visuale", "Fundido a negro al cambiar de cámara", "Przyciemnienie przy zmianie kamery",
				"Затемнение при смене камеры", "視点切り替え時に暗転", "切換鏡頭時淡出黑畫面" },
			{ "From 3rd person: a short fade to black hides the switch to 1st person and back.",
				"Depuis la vue à la 3e personne : un bref fondu au noir masque le passage à la 1re personne et le retour.",
				"Aus der 3rd-Person-Sicht: Eine kurze Abblende verdeckt den Wechsel in die Ego-Sicht und zurück.",
				"Dalla terza persona: una breve dissolvenza al nero nasconde il passaggio alla prima persona e il ritorno.",
				"Desde tercera persona: un breve fundido a negro oculta el cambio a primera persona y la vuelta.",
				"Z widoku trzecioosobowego: krótkie przyciemnienie ukrywa przejście do widoku pierwszoosobowego i z powrotem.",
				"От третьего лица: короткое затемнение скрывает переход к виду от первого лица и обратно.",
				"三人称視点から：短い暗転で一人称視点への切り替えと復帰を隠します。",
				"第三人稱時：短暫的淡出黑畫面會遮住切換到第一人稱與切回的過程。" },
			{ "Fade length", "Durée du fondu", "Abblenddauer", "Durata dissolvenza", "Duración del fundido", "Długość przyciemnienia",
				"Длительность затемнения", "暗転の長さ", "淡出時間" },
			{ "Seconds to black; coming back from black takes a bit longer.", "Secondes jusqu'au noir ; le retour prend un peu plus de temps.",
				"Sekunden bis Schwarz; das Aufblenden dauert etwas länger.", "Secondi fino al nero; il ritorno richiede un po' di più.",
				"Segundos hasta el negro; volver tarda un poco más.", "Sekundy do czerni; powrót trwa nieco dłużej.",
				"Секунды до черноты; возвращение длится чуть дольше.", "暗転までの秒数。明るく戻るのは少し長めです。",
				"變黑所需秒數；恢復畫面會稍久一些。" },
			{ "Weapons and bows are held by the grip; the mouse turns the hand. Other items float over the hand.",
				"Les armes et les arcs se tiennent par la poignée ; la souris tourne la main. Les autres objets flottent au-dessus de la main.",
				"Waffen und Bögen werden am Griff gehalten; die Maus dreht die Hand. Andere Gegenstände schweben über der Hand.",
				"Armi e archi si tengono per l'impugnatura; il mouse ruota la mano. Gli altri oggetti fluttuano sopra la mano.",
				"Las armas y los arcos se sujetan por la empuñadura; el ratón gira la mano. Los demás objetos flotan sobre la mano.",
				"Broń i łuki trzymasz za rękojeść; mysz obraca dłoń. Inne przedmioty unoszą się nad dłonią.",
				"Оружие и луки держатся за рукоять; мышь поворачивает руку. Остальные предметы парят над ладонью.",
				"武器と弓は握って持ち、マウスで手を回します。その他のアイテムは手の上に浮かびます。",
				"武器和弓會握著握把，滑鼠可轉動手部。其他物品會浮在手上。" },

			// Normal pickup for
			{ "Normal pickup for", "Ramassage normal pour", "Normales Aufheben für", "Raccolta normale per", "Recogida normal para",
				"Zwykłe podnoszenie dla", "Обычный подбор для", "通常どおり拾うもの", "以下物品照常拾取" },
			{ "Items taken during combat", "Objets pris en combat", "Gegenstände im Kampf", "Oggetti presi in combattimento",
				"Objetos cogidos en combate", "Przedmioty podniesione w walce", "Предметы, взятые в бою", "戦闘中に拾うアイテム",
				"戰鬥中拾取的物品" },
			{ "Gold", "Or", "Gold", "Oro", "Oro", "Złoto", "Золото", "ゴールド", "金幣" },
			{ "Arrows and bolts", "Flèches et carreaux", "Pfeile und Bolzen", "Frecce e dardi", "Flechas y virotes", "Strzały i bełty",
				"Стрелы и болты", "矢とボルト", "箭矢與弩箭" },
			{ "Weapons", "Armes", "Waffen", "Armi", "Armas", "Broń", "Оружие", "武器", "武器" },
			{ "Swords, axes, bows... go straight into the inventory, like in the normal game.",
				"Épées, haches, arcs... vont directement dans l'inventaire, comme dans le jeu normal.",
				"Schwerter, Äxte, Bögen... kommen wie im normalen Spiel direkt ins Inventar.",
				"Spade, asce, archi... vanno direttamente nell'inventario, come nel gioco normale.",
				"Espadas, hachas, arcos... van directamente al inventario, como en el juego normal.",
				"Miecze, topory, łuki... trafiają od razu do ekwipunku, jak w zwykłej grze.",
				"Мечи, топоры, луки... сразу попадают в инвентарь, как в обычной игре.",
				"剣・斧・弓などは通常どおり直接インベントリに入ります。", "劍、斧、弓等會像原版一樣直接放進物品欄。" },
			{ "Armor and clothes", "Armures et vêtements", "Rüstung und Kleidung", "Armature e vestiti", "Armaduras y ropa",
				"Pancerze i ubrania", "Броня и одежда", "防具と衣服", "護甲與衣物" },
			{ "Armor, clothes and jewelry go straight into the inventory, like in the normal game.",
				"Armures, vêtements et bijoux vont directement dans l'inventaire, comme dans le jeu normal.",
				"Rüstung, Kleidung und Schmuck kommen wie im normalen Spiel direkt ins Inventar.",
				"Armature, vestiti e gioielli vanno direttamente nell'inventario, come nel gioco normale.",
				"Armaduras, ropa y joyas van directamente al inventario, como en el juego normal.",
				"Pancerze, ubrania i biżuteria trafiają od razu do ekwipunku, jak w zwykłej grze.",
				"Броня, одежда и украшения сразу попадают в инвентарь, как в обычной игре.",
				"防具・衣服・装飾品は通常どおり直接インベントリに入ります。", "護甲、衣物與飾品會像原版一樣直接放進物品欄。" },

			// Controls
			{ "Controls", "Commandes", "Steuerung", "Comandi", "Controles", "Sterowanie", "Управление", "操作", "操作" },
			{ "Keyboard / mouse", "Clavier / souris", "Tastatur / Maus", "Tastiera / mouse", "Teclado / ratón", "Klawiatura / mysz",
				"Клавиатура / мышь", "キーボード / マウス", "鍵盤 / 滑鼠" },
			{ "Gamepad", "Manette", "Gamepad", "Controller", "Mando", "Pad", "Геймпад", "ゲームパッド", "手把" },
			{ "Put in backpack", "Mettre dans le sac", "In den Rucksack", "Metti nello zaino", "Guardar en la mochila", "Włóż do plecaka",
				"В рюкзак", "バックパックに入れる", "放進背包" },
			{ "Put back", "Reposer", "Zurücklegen", "Rimetti a posto", "Devolver", "Odłóż", "Вернуть на место", "元に戻す", "放回原處" },
			{ "Look around (hold)", "Regarder autour (maintenir)", "Umsehen (halten)", "Guardati intorno (tieni premuto)",
				"Mirar alrededor (mantener)", "Rozglądanie się (przytrzymaj)", "Осмотреться (удерживать)", "見回す（長押し）",
				"環顧四周（按住）" },
			{ "Mouse speed", "Vitesse de la souris", "Mausgeschwindigkeit", "Velocità mouse", "Velocidad del ratón", "Szybkość myszy",
				"Скорость мыши", "マウス速度", "滑鼠速度" },
			{ "How fast moving the mouse turns the item.", "Vitesse à laquelle la souris tourne l'objet.",
				"Wie schnell die Maus den Gegenstand dreht.", "Quanto velocemente il mouse ruota l'oggetto.",
				"Lo rápido que el ratón gira el objeto.", "Jak szybko mysz obraca przedmiot.", "Как быстро мышь вращает предмет.",
				"マウスでアイテムを回す速さ。", "滑鼠轉動物品的速度。" },
			{ "Gamepad speed", "Vitesse de la manette", "Gamepad-Geschwindigkeit", "Velocità controller", "Velocidad del mando",
				"Szybkość pada", "Скорость геймпада", "ゲームパッド速度", "手把速度" },
			{ "How fast the right stick turns the item.", "Vitesse à laquelle le stick droit tourne l'objet.",
				"Wie schnell der rechte Stick den Gegenstand dreht.", "Quanto velocemente la levetta destra ruota l'oggetto.",
				"Lo rápido que el stick derecho gira el objeto.", "Jak szybko prawa gałka obraca przedmiot.",
				"Как быстро правый стик вращает предмет.", "右スティックでアイテムを回す速さ。", "右搖桿轉動物品的速度。" },
			{ "Invert up / down", "Inverser haut / bas", "Oben / unten umkehren", "Inverti su / giù", "Invertir arriba / abajo",
				"Odwróć góra / dół", "Инвертировать верх / низ", "上下を反転", "上下反轉" },
			{ "Invert looking up / down", "Inverser le regard haut / bas", "Blick oben / unten umkehren", "Inverti lo sguardo su / giù",
				"Invertir la mirada arriba / abajo", "Odwróć patrzenie góra / dół", "Инвертировать взгляд верх / низ", "見回しの上下を反転",
				"環顧時上下反轉" },
			{ "The mouse wheel brings the item closer or moves it farther away.\n"
			  "Hold the look key to turn your head; the hand and the item stay where they are.\n"
			  "Let go and your head turns back to the item.",
				"La molette rapproche ou éloigne l'objet.\n"
				"Maintenez la touche de regard pour tourner la tête ; la main et l'objet restent en place.\n"
				"Relâchez-la et la tête revient vers l'objet.",
				"Das Mausrad holt den Gegenstand näher heran oder schiebt ihn weiter weg.\n"
				"Halte die Umsehen-Taste, um den Kopf zu drehen; Hand und Gegenstand bleiben, wo sie sind.\n"
				"Lässt du los, dreht sich der Kopf zurück zum Gegenstand.",
				"La rotellina avvicina o allontana l'oggetto.\n"
				"Tieni premuto il tasto per guardarti intorno e girare la testa; mano e oggetto restano fermi.\n"
				"Rilascialo e la testa torna verso l'oggetto.",
				"La rueda del ratón acerca o aleja el objeto.\n"
				"Mantén la tecla de mirar para girar la cabeza; la mano y el objeto se quedan donde están.\n"
				"Suéltala y la cabeza vuelve hacia el objeto.",
				"Kółko myszy przybliża lub oddala przedmiot.\n"
				"Przytrzymaj klawisz rozglądania, aby obrócić głowę; dłoń i przedmiot zostają na miejscu.\n"
				"Puść go, a głowa wróci do przedmiotu.",
				"Колесо мыши приближает или отдаляет предмет.\n"
				"Удерживайте клавишу осмотра, чтобы повернуть голову; рука и предмет остаются на месте.\n"
				"Отпустите её, и голова вернётся к предмету.",
				"マウスホイールでアイテムを近づけたり遠ざけたりします。\n"
				"見回しキーを押している間は頭だけが動き、手とアイテムはその場に留まります。\n"
				"離すと視線がアイテムに戻ります。",
				"滑鼠滾輪可拉近或推遠物品。\n按住環顧鍵可轉動頭部，手與物品會留在原處。\n放開後視線會轉回物品。" },
			{ "Press a key...", "Appuyez sur une touche...", "Taste drücken...", "Premi un tasto...", "Pulsa una tecla...",
				"Naciśnij klawisz...", "Нажмите клавишу...", "キーを押してください...", "請按下按鍵..." },
			{ "Click, then press a gamepad button. Esc cancels.", "Cliquez, puis appuyez sur un bouton de la manette. Échap annule.",
				"Klicken, dann eine Gamepad-Taste drücken. Esc bricht ab.", "Fai clic, poi premi un tasto del controller. Esc annulla.",
				"Haz clic y pulsa un botón del mando. Esc cancela.", "Kliknij, a potem naciśnij przycisk pada. Esc anuluje.",
				"Щёлкните, затем нажмите кнопку геймпада. Esc — отмена.", "クリックしてからゲームパッドのボタンを押します。Escでキャンセル。",
				"點擊後按下手把按鈕。Esc 取消。" },
			{ "Click, then press a key or mouse button. Esc cancels.",
				"Cliquez, puis appuyez sur une touche ou un bouton de la souris. Échap annule.",
				"Klicken, dann eine Taste oder Maustaste drücken. Esc bricht ab.",
				"Fai clic, poi premi un tasto o un pulsante del mouse. Esc annulla.", "Haz clic y pulsa una tecla o un botón del ratón. Esc cancela.",
				"Kliknij, a potem naciśnij klawisz lub przycisk myszy. Esc anuluje.",
				"Щёлкните, затем нажмите клавишу или кнопку мыши. Esc — отмена.", "クリックしてからキーかマウスボタンを押します。Escでキャンセル。",
				"點擊後按下鍵盤按鍵或滑鼠按鈕。Esc 取消。" },

			// Hand / item position and rotation
			{ "Hand position (game units from your eyes)", "Position de la main (unités du jeu depuis vos yeux)",
				"Handposition (Spieleinheiten von deinen Augen)", "Posizione della mano (unità di gioco dagli occhi)",
				"Posición de la mano (unidades del juego desde los ojos)", "Położenie dłoni (jednostki gry od oczu)",
				"Положение руки (игровые единицы от глаз)", "手の位置（目からのゲーム単位）", "手的位置（距離眼睛的遊戲單位）" },
			{ "Right", "Droite", "Rechts", "Destra", "Derecha", "W prawo", "Вправо", "右", "向右" },
			{ "Game units to the right of your eyes.", "Unités du jeu à droite de vos yeux.", "Spieleinheiten rechts von deinen Augen.",
				"Unità di gioco a destra degli occhi.", "Unidades del juego a la derecha de los ojos.", "Jednostki gry na prawo od oczu.",
				"Игровые единицы вправо от глаз.", "目から右へのゲーム単位。", "眼睛右方的遊戲單位。" },
			{ "Forward", "Avant", "Vorne", "Avanti", "Adelante", "Do przodu", "Вперёд", "前", "向前" },
			{ "Game units in front of your eyes.", "Unités du jeu devant vos yeux.", "Spieleinheiten vor deinen Augen.",
				"Unità di gioco davanti agli occhi.", "Unidades del juego delante de los ojos.", "Jednostki gry przed oczami.",
				"Игровые единицы перед глазами.", "目から前へのゲーム単位。", "眼睛前方的遊戲單位。" },
			{ "Up", "Haut", "Oben", "Su", "Arriba", "W górę", "Вверх", "上", "向上" },
			{ "Game units above (negative: below) your eyes.", "Unités du jeu au-dessus (négatif : en dessous) de vos yeux.",
				"Spieleinheiten über (negativ: unter) deinen Augen.", "Unità di gioco sopra (negativo: sotto) gli occhi.",
				"Unidades del juego por encima (negativo: por debajo) de los ojos.", "Jednostki gry nad oczami (ujemne: pod).",
				"Игровые единицы выше глаз (отрицательные — ниже).", "目から上へのゲーム単位（マイナスで下）。", "眼睛上方的遊戲單位（負數為下方）。" },
			{ "Height above the hand", "Hauteur au-dessus de la main", "Höhe über der Hand", "Altezza sopra la mano", "Altura sobre la mano",
				"Wysokość nad dłonią", "Высота над рукой", "手からの高さ", "離手的高度" },
			{ "How far the item floats above your palm.", "Distance à laquelle l'objet flotte au-dessus de la paume.",
				"Wie hoch der Gegenstand über deiner Handfläche schwebt.", "Quanto in alto l'oggetto fluttua sopra il palmo.",
				"A qué altura flota el objeto sobre la palma.", "Jak wysoko przedmiot unosi się nad dłonią.",
				"Как высоко предмет парит над ладонью.", "アイテムが手のひらの上に浮かぶ高さ。", "物品浮在手掌上方的距離。" },
			{ "Item position", "Position de l'objet", "Gegenstandsposition", "Posizione dell'oggetto", "Posición del objeto",
				"Położenie przedmiotu", "Положение предмета", "アイテムの位置", "物品位置" },
			{ "Item right", "Objet à droite", "Gegenstand rechts", "Oggetto a destra", "Objeto a la derecha", "Przedmiot w prawo",
				"Предмет вправо", "アイテム 右", "物品向右" },
			{ "Moves the item to the right (+) or left (-) of its place over the hand.",
				"Déplace l'objet à droite (+) ou à gauche (-) de sa place au-dessus de la main.",
				"Verschiebt den Gegenstand nach rechts (+) oder links (-) von seinem Platz über der Hand.",
				"Sposta l'oggetto a destra (+) o a sinistra (-) rispetto alla sua posizione sopra la mano.",
				"Mueve el objeto a la derecha (+) o a la izquierda (-) de su sitio sobre la mano.",
				"Przesuwa przedmiot w prawo (+) lub w lewo (-) od jego miejsca nad dłonią.",
				"Сдвигает предмет вправо (+) или влево (-) от его места над рукой.",
				"手の上の位置からアイテムを右（+）または左（-）へ動かします。", "將物品從手上方的位置向右（+）或向左（-）移動。" },
			{ "Item forward", "Objet vers l'avant", "Gegenstand vorne", "Oggetto in avanti", "Objeto hacia delante", "Przedmiot do przodu",
				"Предмет вперёд", "アイテム 前", "物品向前" },
			{ "Moves the item away from you (+) or closer (-).", "Éloigne (+) ou rapproche (-) l'objet.",
				"Schiebt den Gegenstand weg (+) oder näher heran (-).", "Allontana (+) o avvicina (-) l'oggetto.",
				"Aleja (+) o acerca (-) el objeto.", "Oddala (+) lub przybliża (-) przedmiot.", "Отодвигает (+) или приближает (-) предмет.",
				"アイテムを遠ざけ（+）たり近づけ（-）たりします。", "將物品推遠（+）或拉近（-）。" },
			{ "Item up", "Objet vers le haut", "Gegenstand oben", "Oggetto in alto", "Objeto hacia arriba", "Przedmiot w górę",
				"Предмет вверх", "アイテム 上", "物品向上" },
			{ "Moves the item up (+) or down (-).", "Monte (+) ou descend (-) l'objet.",
				"Verschiebt den Gegenstand nach oben (+) oder unten (-).", "Sposta l'oggetto in alto (+) o in basso (-).",
				"Sube (+) o baja (-) el objeto.", "Przesuwa przedmiot w górę (+) lub w dół (-).", "Сдвигает предмет вверх (+) или вниз (-).",
				"アイテムを上（+）または下（-）へ動かします。", "將物品向上（+）或向下（-）移動。" },
			{ "Largest item size", "Taille maximale des objets", "Maximale Gegenstandsgröße", "Dimensione massima oggetti",
				"Tamaño máximo de objetos", "Największy rozmiar przedmiotu", "Наибольший размер предмета", "アイテムの最大サイズ",
				"物品最大尺寸" },
			{ "Bigger items (shields, armor...) are shown smaller so they fit in the hand. Weapons keep their size.",
				"Les objets plus grands (boucliers, armures...) sont réduits pour tenir dans la main. Les armes gardent leur taille.",
				"Größere Gegenstände (Schilde, Rüstung...) werden kleiner gezeigt, damit sie in die Hand passen. Waffen behalten ihre Größe.",
				"Gli oggetti più grandi (scudi, armature...) vengono rimpiccioliti per stare in mano. Le armi mantengono la loro dimensione.",
				"Los objetos más grandes (escudos, armaduras...) se muestran más pequeños para que quepan en la mano. Las armas conservan su tamaño.",
				"Większe przedmioty (tarcze, pancerze...) są pomniejszane, aby zmieściły się w dłoni. Broń zachowuje swój rozmiar.",
				"Крупные предметы (щиты, броня...) уменьшаются, чтобы поместиться в руке. Оружие сохраняет свой размер.",
				"大きなアイテム（盾・防具など）は手に収まるよう小さく表示されます。武器は元の大きさのままです。",
				"較大的物品（盾牌、護甲等）會縮小以放進手中。武器保持原本大小。" },
			{ "Hand rotation (degrees)", "Rotation de la main (degrés)", "Handdrehung (Grad)", "Rotazione della mano (gradi)",
				"Rotación de la mano (grados)", "Obrót dłoni (stopnie)", "Поворот руки (градусы)", "手の回転（度）", "手的旋轉（度）" },
			{ "Turn", "Tourner", "Drehen", "Ruota", "Girar", "Obrót", "Поворот", "回転", "轉向" },
			{ "Turns the hand to the right (+) or left (-).", "Tourne la main vers la droite (+) ou la gauche (-).",
				"Dreht die Hand nach rechts (+) oder links (-).", "Ruota la mano a destra (+) o a sinistra (-).",
				"Gira la mano a la derecha (+) o a la izquierda (-).", "Obraca dłoń w prawo (+) lub w lewo (-).",
				"Поворачивает руку вправо (+) или влево (-).", "手を右（+）または左（-）へ向けます。", "將手向右（+）或向左（-）轉。" },
			{ "Tilt", "Incliner", "Neigen", "Inclina", "Inclinar", "Pochylenie", "Наклон", "傾き", "傾斜" },
			{ "Tilts the fingers up (+) or down (-).", "Incline les doigts vers le haut (+) ou le bas (-).",
				"Neigt die Finger nach oben (+) oder unten (-).", "Inclina le dita verso l'alto (+) o il basso (-).",
				"Inclina los dedos hacia arriba (+) o hacia abajo (-).", "Pochyla palce w górę (+) lub w dół (-).",
				"Наклоняет пальцы вверх (+) или вниз (-).", "指を上（+）または下（-）へ傾けます。", "將手指向上（+）或向下（-）傾斜。" },
			{ "Roll", "Rouler", "Rollen", "Rollio", "Rotar", "Przechył", "Крен", "ひねり", "翻轉" },
			{ "Rolls the palm around the fingers.", "Fait pivoter la paume autour des doigts.", "Rollt die Handfläche um die Finger.",
				"Ruota il palmo attorno alle dita.", "Gira la palma alrededor de los dedos.", "Obraca dłoń wokół palców.",
				"Поворачивает ладонь вокруг пальцев.", "指を軸に手のひらをひねります。", "以手指為軸翻轉手掌。" },

			// Weapons and bows
			{ "Weapons (held by the grip)", "Armes (tenues par la poignée)", "Waffen (am Griff gehalten)", "Armi (tenute per l'impugnatura)",
				"Armas (sujetas por la empuñadura)", "Broń (trzymana za rękojeść)", "Оружие (за рукоять)", "武器（握って持つ）",
				"武器（握住握把）" },
			{ "Weapon hand right", "Main (arme) à droite", "Waffenhand rechts", "Mano dell'arma a destra", "Mano del arma a la derecha",
				"Dłoń z bronią w prawo", "Рука с оружием вправо", "武器の手 右", "持武器的手向右" },
			{ "Weapon hand forward", "Main (arme) vers l'avant", "Waffenhand vorne", "Mano dell'arma in avanti", "Mano del arma hacia delante",
				"Dłoń z bronią do przodu", "Рука с оружием вперёд", "武器の手 前", "持武器的手向前" },
			{ "Weapon hand up", "Main (arme) vers le haut", "Waffenhand oben", "Mano dell'arma in alto", "Mano del arma hacia arriba",
				"Dłoń z bronią w górę", "Рука с оружием вверх", "武器の手 上", "持武器的手向上" },
			{ "Blade lean left", "Inclinaison de la lame à gauche", "Klinge nach links neigen", "Inclinazione lama a sinistra",
				"Inclinación de la hoja a la izquierda", "Pochylenie ostrza w lewo", "Наклон клинка влево", "刃の左への傾き", "刀刃向左傾斜" },
			{ "Degrees the blade tips to the left (-: right) from straight up.",
				"Degrés d'inclinaison de la lame vers la gauche (- : droite) depuis la verticale.",
				"Grad, um die sich die Klinge aus der Senkrechten nach links neigt (-: rechts).",
				"Gradi di inclinazione della lama a sinistra (-: destra) rispetto alla verticale.",
				"Grados que la hoja se inclina a la izquierda (-: derecha) desde la vertical.",
				"O ile stopni ostrze pochyla się w lewo (-: w prawo) od pionu.",
				"На сколько градусов клинок наклонён влево (-: вправо) от вертикали.", "垂直から刃を左へ傾ける角度（-で右）。",
				"刀刃從垂直向左傾斜的角度（負數向右）。" },
			{ "Blade lean forward", "Inclinaison de la lame vers l'avant", "Klinge nach vorne neigen", "Inclinazione lama in avanti",
				"Inclinación de la hoja hacia delante", "Pochylenie ostrza do przodu", "Наклон клинка вперёд", "刃の前への傾き", "刀刃向前傾斜" },
			{ "Degrees the blade tips away from you (-: towards you).", "Degrés d'inclinaison de la lame loin de vous (- : vers vous).",
				"Grad, um die sich die Klinge von dir weg neigt (-: zu dir hin).",
				"Gradi di inclinazione della lama lontano da te (-: verso di te).",
				"Grados que la hoja se inclina alejándose de ti (-: hacia ti).", "O ile stopni ostrze pochyla się od ciebie (-: do ciebie).",
				"На сколько градусов клинок наклонён от вас (-: к вам).", "刃を奥へ傾ける角度（-で手前）。",
				"刀刃向遠離你的方向傾斜的角度（負數朝向你）。" },
			{ "Blade roll", "Rotation de la lame", "Klinge drehen", "Rotazione lama", "Giro de la hoja", "Obrót ostrza", "Поворот клинка",
				"刃のひねり", "刀刃翻轉" },
			{ "Turns the weapon around the blade.", "Fait tourner l'arme autour de la lame.", "Dreht die Waffe um die Klinge.",
				"Ruota l'arma attorno alla lama.", "Gira el arma alrededor de la hoja.", "Obraca broń wokół ostrza.",
				"Поворачивает оружие вокруг клинка.", "刃を軸に武器をひねります。", "以刀刃為軸轉動武器。" },
			{ "Bows and crossbows", "Arcs et arbalètes", "Bögen und Armbrüste", "Archi e balestre", "Arcos y ballestas", "Łuki i kusze",
				"Луки и арбалеты", "弓とクロスボウ", "弓與弩" },
			{ "Bow hand right", "Main (arc) à droite", "Bogenhand rechts", "Mano dell'arco a destra", "Mano del arco a la derecha",
				"Dłoń z łukiem w prawo", "Рука с луком вправо", "弓の手 右", "持弓的手向右" },
			{ "Bow hand forward", "Main (arc) vers l'avant", "Bogenhand vorne", "Mano dell'arco in avanti", "Mano del arco hacia delante",
				"Dłoń z łukiem do przodu", "Рука с луком вперёд", "弓の手 前", "持弓的手向前" },
			{ "Game units in front of your eyes. Your arm reaches about 36, further it stays stretched.",
				"Unités du jeu devant vos yeux. Votre bras atteint environ 36 ; au-delà, il reste tendu.",
				"Spieleinheiten vor deinen Augen. Dein Arm reicht etwa 36 weit, darüber hinaus bleibt er gestreckt.",
				"Unità di gioco davanti agli occhi. Il braccio arriva a circa 36; oltre resta disteso.",
				"Unidades del juego delante de los ojos. Tu brazo llega a unos 36; más allá se queda estirado.",
				"Jednostki gry przed oczami. Ramię sięga około 36, dalej pozostaje wyprostowane.",
				"Игровые единицы перед глазами. Рука дотягивается примерно до 36, дальше остаётся вытянутой.",
				"目から前へのゲーム単位。腕が届くのは約36までで、それ以上は伸びたままです。",
				"眼睛前方的遊戲單位。手臂約可伸到 36，超過時會保持伸直。" },
			{ "Bow hand up", "Main (arc) vers le haut", "Bogenhand oben", "Mano dell'arco in alto", "Mano del arco hacia arriba",
				"Dłoń z łukiem w górę", "Рука с луком вверх", "弓の手 上", "持弓的手向上" },
			{ "Bow lean left", "Inclinaison de l'arc à gauche", "Bogen nach links neigen", "Inclinazione arco a sinistra",
				"Inclinación del arco a la izquierda", "Pochylenie łuku w lewo", "Наклон лука влево", "弓の左への傾き", "弓向左傾斜" },
			{ "Degrees the bow tips to the left (-: right) from straight up.",
				"Degrés d'inclinaison de l'arc vers la gauche (- : droite) depuis la verticale.",
				"Grad, um die sich der Bogen aus der Senkrechten nach links neigt (-: rechts).",
				"Gradi di inclinazione dell'arco a sinistra (-: destra) rispetto alla verticale.",
				"Grados que el arco se inclina a la izquierda (-: derecha) desde la vertical.",
				"O ile stopni łuk pochyla się w lewo (-: w prawo) od pionu.",
				"На сколько градусов лук наклонён влево (-: вправо) от вертикали.", "垂直から弓を左へ傾ける角度（-で右）。",
				"弓從垂直向左傾斜的角度（負數向右）。" },
			{ "Bow lean forward", "Inclinaison de l'arc vers l'avant", "Bogen nach vorne neigen", "Inclinazione arco in avanti",
				"Inclinación del arco hacia delante", "Pochylenie łuku do przodu", "Наклон лука вперёд", "弓の前への傾き", "弓向前傾斜" },
			{ "Degrees the bow tips away from you (-: towards you).", "Degrés d'inclinaison de l'arc loin de vous (- : vers vous).",
				"Grad, um die sich der Bogen von dir weg neigt (-: zu dir hin).",
				"Gradi di inclinazione dell'arco lontano da te (-: verso di te).",
				"Grados que el arco se inclina alejándose de ti (-: hacia ti).", "O ile stopni łuk pochyla się od ciebie (-: do ciebie).",
				"На сколько градусов лук наклонён от вас (-: к вам).", "弓を奥へ傾ける角度（-で手前）。",
				"弓向遠離你的方向傾斜的角度（負數朝向你）。" },

			// Animation
			{ "Animation (seconds)", "Animation (secondes)", "Animation (Sekunden)", "Animazione (secondi)", "Animación (segundos)",
				"Animacja (sekundy)", "Анимация (секунды)", "アニメーション（秒）", "動畫（秒）" },
			{ "Hand comes up", "La main se lève", "Hand hebt sich", "La mano si alza", "La mano sube", "Dłoń się unosi",
				"Рука поднимается", "手を上げる", "手抬起" },
			{ "Hand goes to the backpack", "La main va au sac", "Hand geht zum Rucksack", "La mano va allo zaino", "La mano va a la mochila",
				"Dłoń sięga do plecaka", "Рука к рюкзаку", "手をバックパックへ", "手伸向背包" },
			{ "Hand comes back", "La main revient", "Hand kommt zurück", "La mano torna", "La mano vuelve", "Dłoń wraca",
				"Рука возвращается", "手を戻す", "手收回" },
			{ "Reset to defaults", "Valeurs par défaut", "Auf Standard zurücksetzen", "Ripristina predefiniti", "Restablecer valores",
				"Przywróć domyślne", "Сбросить по умолчанию", "初期設定に戻す", "恢復預設值" },

			// Languages page
			{ "Language", "Langue", "Sprache", "Lingua", "Idioma", "Język", "Язык", "言語", "語言" },
			{ "Automatic (game language)", "Automatique (langue du jeu)", "Automatisch (Spielsprache)", "Automatica (lingua del gioco)",
				"Automático (idioma del juego)", "Automatycznie (język gry)", "Автоматически (язык игры)", "自動（ゲームの言語）",
				"自動（遊戲語言）" },
			{ "The key hint on screen uses the game's own font: with a language the game isn't set to, some letters may be missing there.",
				"L'aide des touches à l'écran utilise la police du jeu : dans une autre langue que celle du jeu, certaines lettres peuvent manquer.",
				"Der Tastenhinweis im Spiel nutzt die Schrift des Spiels: In einer anderen Sprache als der des Spiels können dort Buchstaben fehlen.",
				"Il suggerimento tasti a schermo usa il carattere del gioco: in una lingua diversa da quella del gioco alcune lettere potrebbero mancare.",
				"La ayuda de teclas en pantalla usa la fuente del juego: en un idioma distinto al del juego pueden faltar algunas letras.",
				"Podpowiedź klawiszy na ekranie używa czcionki gry: w języku innym niż język gry może brakować niektórych liter.",
				"Подсказка клавиш на экране использует шрифт игры: на языке, отличном от языка игры, некоторые буквы могут не отображаться.",
				"画面上のキーのヒントはゲームのフォントを使います。ゲームと異なる言語では一部の文字が表示されない場合があります。",
				"畫面上的按鍵提示使用遊戲本身的字型：若與遊戲語言不同，部分文字可能無法顯示。" },
		};

		const std::unordered_map<std::string_view, const Entry*>& Table()
		{
			static const auto table = [] {
				std::unordered_map<std::string_view, const Entry*> map;
				for (const auto& entry : kTexts) {
					map.emplace(entry[0], &entry);
				}
				return map;
			}();
			return table;
		}

		std::atomic<int> current{ 1 };

		int GameLanguage()
		{
			const auto ini = RE::INISettingCollection::GetSingleton();
			const auto setting = ini ? ini->GetSetting("sLanguage:General") : nullptr;
			const char* name = setting ? setting->GetString() : nullptr;
			if (name) {
				std::string upper{ name };
				std::ranges::transform(upper, upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
				for (int i = 0; i < kCount; ++i) {
					if (upper == kGameNames[i]) {
						return i + 1;
					}
				}
			}
			return 1;
		}
	}

	void Set(int a_setting)
	{
		const int language = a_setting >= 1 && a_setting <= kCount ? a_setting : GameLanguage();
		current = language;
		Table();  // built here, before the menu and the HUD read from other threads
		logs::info("Language: {}{}", kNativeNames[language - 1], a_setting == kAuto ? " (the game's)" : "");
	}

	int Current()
	{
		return current;
	}

	const char* NativeName(int a_language)
	{
		return a_language >= 1 && a_language <= kCount ? kNativeNames[a_language - 1] : "";
	}

	const char* T(const char* a_english)
	{
		const int language = current;
		if (language <= 1) {
			return a_english;
		}
		const auto& table = Table();
		const auto  found = table.find(a_english);
		return found != table.end() ? (*found->second)[language - 1] : a_english;
	}
}
