"""A data-only domain mapping over an existing addressable source handle.

No provider names or format branches. The selected mapping is a projection, not
an asserted interpretation of unknown domain semantics. Source selectors survive.
"""
from copy import deepcopy
from .core import _SyntaxHandle, ResourceError
from .discovery import apply_mapping, validate_mapping


class MappingAdapter:
    def __init__(self, source_adapter, mapping):
        self.source_adapter = source_adapter
        self.mapping = deepcopy(validate_mapping(mapping))

    def open(self, resource, access):
        handle = self.source_adapter.open(resource, access)
        # This adapter supports bounded, parsed documents. It is explicit about
        # that scope; seekable sources use their own adapter without this scan.
        try:
            value = handle.select('')
            records = apply_mapping(value, self.mapping, source={
                'logical_id': resource['logical_id'], 'source_version': handle.metadata().get('source_version')}, inline=False)
            metadata = dict(handle.metadata(), mapping_version=self.mapping['version'],
                            mapping_id=self.mapping['id'], recognition='structural_mapping',
                            read_scope='selected_document', domain_semantics=self.mapping.get('domain_semantics','unrecognized'))
            return _SyntaxHandle(records, metadata)
        finally:
            if hasattr(handle, 'close'):
                handle.close()
