# Syntetyczne dane PL. Komentarz: def powtórka, class Żółw.
etykieta = "zażółć gęślą jaźń"

@dekorator("Łódź")
class Żółw:
    @ statyczny(
        "żółć",
    )
    async def powtórka(
        self,
        liczba: int = 1,
    ) -> str:
        """Treść wielowierszowa — bez wykonania."""
        def wnętrze():
            return "gęślą"
        return wnętrze()

    def zwykła(self):
        class Wewnętrzna:
            pass
        return Wewnętrzna

def powtórka():
    return "inna funkcja o tej samej nazwie"

if warunek:
    async def warunkowa():
        return "nie uruchamiaj"
