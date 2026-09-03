"""Shared dataset display names and plotting order."""

from __future__ import annotations

from typing import TypedDict


class DatasetMetadata(TypedDict):
    dataset_name: str
    display_name: str
    full_name: str


DATASETS: list[DatasetMetadata] = [
    {"dataset_name": "google_plus", "display_name": "GOOG", "full_name": "Google+"},
    {"dataset_name": "friendster", "display_name": "FRND", "full_name": "Friendster"},
    {"dataset_name": "twitter", "display_name": "TWIT", "full_name": "Twitter"},
    {"dataset_name": "com-orkut", "display_name": "ORKT", "full_name": "Orkut"},
    {"dataset_name": "enwiki", "display_name": "EN\nWIKI", "full_name": "ENWiki"},
    {"dataset_name": "com-youtube", "display_name": "YT", "full_name": "Youtube"},
    {"dataset_name": "RoadUSA", "display_name": "ROAD\nUSA", "full_name": "RoadUSA"},
    {"dataset_name": "Germany", "display_name": "ROAD\nGER", "full_name": "RoadGER"},
    {"dataset_name": "sift_RS-50K", "display_name": "SIFT\nRS", "full_name": "SIFT-RS-50K"},
    {"dataset_name": "sift_KNN-500", "display_name": "SIFT\nKNN", "full_name": "SIFT-KNN-500"},
    {"dataset_name": "msspace_RS-10K", "display_name": "MSSP\nRS", "full_name": "MSSPACE-RS-10K"},
    {"dataset_name": "msspace_KNN-500", "display_name": "MSSP\nKNN", "full_name": "MSSPACE-KNN-500"},
    {"dataset_name": "kron_13", "display_name": "KRON\n13", "full_name": "kron-13"},
    {"dataset_name": "kron_15", "display_name": "KRON\n15", "full_name": "kron-15"},
    {"dataset_name": "kron_16", "display_name": "KRON\n16", "full_name": "kron-16"},
]


def canonical_dataset_name(dataset: str) -> str:
    return dataset[:-4] if dataset.endswith("_sym") else dataset


def order_datasets(datasets: list[str]) -> tuple[list[str], list[str]]:
    metadata_by_name = {entry["dataset_name"]: entry for entry in DATASETS}
    original_by_canonical = {canonical_dataset_name(dataset): dataset for dataset in datasets}
    matched = [
        original_by_canonical[entry["dataset_name"]]
        for entry in DATASETS
        if entry["dataset_name"] in original_by_canonical
    ]
    unmatched = sorted(
        dataset for dataset in datasets
        if canonical_dataset_name(dataset) not in metadata_by_name
    )
    if unmatched:
        print("Unmatched datasets appended after configured order: " + ", ".join(unmatched))
    ordered = matched + unmatched
    labels = [
        metadata_by_name[canonical_dataset_name(dataset)]["display_name"]
        if canonical_dataset_name(dataset) in metadata_by_name else dataset
        for dataset in ordered
    ]
    return ordered, labels


def full_dataset_name(dataset: str) -> str:
    canonical = canonical_dataset_name(dataset)
    metadata_by_name = {entry["dataset_name"]: entry for entry in DATASETS}
    return metadata_by_name[canonical]["full_name"] if canonical in metadata_by_name else dataset
