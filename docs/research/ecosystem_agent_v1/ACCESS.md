# Dostęp do OpenRoutera i GCP dla następnego eksperymentu

OpenRouter dostarcza model decyzyjny; GCP dostarcza środowisko wykonania. Są
niezależnymi adapterami wspólnego agenta. Lokalny prototyp ich nie wymaga.

Do płatnego eksperymentu potrzebne są wybrane modele, parametry wywołań i
zatwierdzony budżet. Sekret trafia do rzeczywistego magazynu sekretów wykonawcy;
obecny manifest OpenRoutera w Loomie odwołuje się do `api_key`. Referencja
sekretu jest parametrem kompilatora; nie powstaje nowy równoległy magazyn kluczy.
Nie wolno umieszczać wartości sekretu w serializowanym `Environment.config`,
ponieważ prototyp zapisuje tę konfigurację w journalu. Adapter provider musi
rozwiązać referencję poza danymi planera i raportem.

Do próby na istniejącej maszynie GCP potrzebne są project ID, strefa,
instance ID, wybrana metoda połączenia i poświadczenia skonfigurowane w miejscu,
z którego faktycznie połączy się wykonawca. Preferowana ścieżka to ADC z
impersonacją konta serwisowego lub konto podpięte do zasobu GCP. Dokładny zakres
IAM zależy od wybranej metody dostępu; wykonywanie zadania na istniejącej VM
jest odrębne od tworzenia/usuwania VM. W tym etapie nie wybieramy wielkości VM
i nie przyjmujemy budżetu domyślnego.

`connections.py` opisuje te dane i waliduje kontrakt offline. Następny krok
wymaga adaptera rozmowy, trwałej rezerwacji każdego opłacanego żądania,
rozwiązania sekretu oraz odczytu i próby wybranej VM. Nie ma jeszcze połączenia
do kont z tej sesji; wyszukiwanie pluginów nie znalazło odpowiedniego connectora,
a `gcloud` nie jest dostępny. Klucze nie należą do rozmowy ani repozytorium.

| Pole | Wartość na teraz |
|---|---|
| OpenRouter modele / routing / parametry | Do wyboru właściciela |
| Budżet nowych prób | Nie ustalono; nie rozszerza istniejącego budżetu Jev |
| OpenRouter referencja sekretu | `api_key` lub jawne mapowanie innego magazynu |
| GCP project / zone / instance | Nie podano |
| GCP istniejąca VM czy provisioning | Do wyboru właściciela |
| GCP tożsamość i metoda połączenia | Do konfiguracji w docelowym wykonawcy |

Źródła: [OpenRouter authentication](https://openrouter.ai/docs/api_reference/authentication),
[Google Compute Engine authentication](https://docs.cloud.google.com/compute/docs/authentication),
[service account impersonation](https://docs.cloud.google.com/docs/authentication/use-service-account-impersonation).
