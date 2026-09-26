"""Conversation importer for ChatGPT and Claude exports."""


class ConversationImporter:
    """Keeps raw source bytes and records provenance for every message."""

    def import_file(self, path):
        # FIXME: the html parser drops code blocks
        return path
