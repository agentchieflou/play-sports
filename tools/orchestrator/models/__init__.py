"""Model client layer (Epic 135).

Epic 119's future MCP Model Router Service wraps this package - extend it
here, never twin it elsewhere.
"""

from .base import ChatResponse, Message, ModelClient, ProviderError, ToolCall, ToolSpec, Usage
from .router import ModelRouter

__all__ = [
    "ChatResponse",
    "Message",
    "ModelClient",
    "ModelRouter",
    "ProviderError",
    "ToolCall",
    "ToolSpec",
    "Usage",
]
