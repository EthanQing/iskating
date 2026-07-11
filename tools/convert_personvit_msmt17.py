from __future__ import annotations

import argparse
from collections import OrderedDict
from pathlib import Path

import torch
from torch import nn


class Mlp(nn.Module):
    def __init__(self, hidden_size: int, intermediate_size: int) -> None:
        super().__init__()
        self.fc1 = nn.Linear(hidden_size, intermediate_size)
        self.act = nn.GELU()
        self.fc2 = nn.Linear(intermediate_size, hidden_size)

    def forward(self, value: torch.Tensor) -> torch.Tensor:
        return self.fc2(self.act(self.fc1(value)))


class Attention(nn.Module):
    def __init__(self, hidden_size: int, heads: int) -> None:
        super().__init__()
        self.heads = heads
        self.head_size = hidden_size // heads
        self.qkv = nn.Linear(hidden_size, hidden_size * 3)
        self.proj = nn.Linear(hidden_size, hidden_size)

    def forward(self, value: torch.Tensor) -> torch.Tensor:
        batch, tokens, hidden_size = value.shape
        qkv = self.qkv(value).reshape(batch, tokens, 3, self.heads, self.head_size)
        qkv = qkv.permute(2, 0, 3, 1, 4)
        query, key, current_value = qkv.unbind(0)
        attention = (query @ key.transpose(-2, -1)) / self.head_size**0.5
        attention = attention.softmax(dim=-1)
        output = attention @ current_value
        output = output.transpose(1, 2).reshape(batch, tokens, hidden_size)
        return self.proj(output)


class PatchEmbed(nn.Module):
    def __init__(self) -> None:
        super().__init__()
        self.proj = nn.Conv2d(3, 768, kernel_size=16, stride=16)

    def forward(self, image: torch.Tensor) -> torch.Tensor:
        return self.proj(image)


class Block(nn.Module):
    def __init__(self, hidden_size: int, heads: int, intermediate_size: int) -> None:
        super().__init__()
        self.norm1 = nn.LayerNorm(hidden_size, eps=1e-6)
        self.attn = Attention(hidden_size, heads)
        self.norm2 = nn.LayerNorm(hidden_size, eps=1e-6)
        self.mlp = Mlp(hidden_size, intermediate_size)

    def forward(self, value: torch.Tensor) -> torch.Tensor:
        value = value + self.attn(self.norm1(value))
        return value + self.mlp(self.norm2(value))


class PersonVitEmbedding(nn.Module):
    def __init__(self) -> None:
        super().__init__()
        self.cls_token = nn.Parameter(torch.zeros(1, 1, 768))
        self.patch_embed = PatchEmbed()
        self.pos_embed = nn.Parameter(torch.zeros(1, 129, 768))
        self.blocks = nn.ModuleList([Block(768, 12, 3072) for _ in range(12)])
        self.norm = nn.LayerNorm(768, eps=1e-6)

    def forward(self, image: torch.Tensor) -> torch.Tensor:
        tokens = self.patch_embed(image).flatten(2).transpose(1, 2)
        cls_token = self.cls_token.expand(image.shape[0], -1, -1)
        tokens = torch.cat((cls_token, tokens), dim=1) + self.pos_embed
        for block in self.blocks:
            tokens = block(tokens)
        embedding = self.norm(tokens[:, 0])
        return nn.functional.normalize(embedding, p=2, dim=1)


def load_base_weights(model: PersonVitEmbedding, checkpoint: Path) -> None:
    state = torch.load(checkpoint, map_location="cpu")
    if not isinstance(state, OrderedDict):
        state = state.get("state_dict", state.get("model", state))
    converted = {}
    for key, value in state.items():
        if key.startswith("base."):
            converted[key[5:]] = value
    converted.pop("fc.weight", None)
    converted.pop("fc.bias", None)
    model.load_state_dict(converted, strict=True)


def main() -> None:
    parser = argparse.ArgumentParser(description="Export the TransReID MSMT17 ViT-Base embedding model.")
    parser.add_argument("--checkpoint", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    model = PersonVitEmbedding().eval()
    load_base_weights(model, args.checkpoint)
    example = torch.zeros(1, 3, 256, 128)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    torch.onnx.export(
        model,
        example,
        args.output,
        input_names=["images"],
        output_names=["embedding"],
        opset_version=17,
        do_constant_folding=True,
        dynamo=False,
    )
    print(f"Exported {args.output}")


if __name__ == "__main__":
    main()
