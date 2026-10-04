# Wątek 12 — onboarding, profil użytkownika, warstwy domyślnych

Baza: `30ad7d37337d6641cb7714b03e9feff0e6e25d25`; gałąź `gpt/onboarding-2026-10-04`.
Status: implementacja trwa; ten checkpoint dokumentuje przydział, nie gotowość.
Testy wyłącznie offline, dane syntetyczne, bez wywołań dostawców.

## Do wątku 9

Właściciel przeniósł onboarding i R39–R40 z 10 do 12. Proszę uwzględnić ten przydział w INDEX. W12 nie edytuje STATE/INDEX/main. W11 RuntimeProfile i W3/W4 metody pozostają zależnościami, bez przejmowania ich plików.

## Do wątku 10

W12 dostarcza komponenty wyłącznie w `loom/web/src/onboarding/`. W10 odpowiada za wpięcie w App.tsx, nawigację i istniejący transport aplikacji. Formularz i kreator korzystają z jednego natywnego stanu; modelowy transport musi użyć sprawdzonego żądania po filtracji prywatności i polityki zużycia W2.

## Do wątku 11

W12 korzysta z kontraktu RuntimeProfile::from_definition/with_values. Loader na main jeszcze nie występuje; adapter jawnie zgłasza unavailable do chwili przyjęcia foundation. Wykluczenia potrzebują trwałego stanu oddzielnego od RFC6902 usunięcia klucza, które samo nie chroni przed aktualizacją packa.

## Do wątku 3

API kategorii prywatności i grafowy profil zostaną opisane przy implementacji. Stan unknown/declined/never jest polityką pytania/wnioskowania; user_stated/model_inferred/form opisują akwizycję i review, nie zastępują kanonicznych Origin/EvidenceClass.
